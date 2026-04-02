//! Async runtime for HEXA — cooperative green-thread scheduler.
//!
//! Provides a task-based concurrency model where `async fn` returns a Future,
//! `await` suspends the current task, and `select` waits for the first of N
//! channels/futures/timeouts.
//!
//! Architecture:
//!   - TaskId: unique identifier for each green thread
//!   - Task: suspended computation with state (Ready/Running/Waiting/Complete)
//!   - Scheduler: round-robin cooperative scheduler with a task queue
//!   - Future: value that will be available later (resolved by a task)
//!   - Timer: deadline-based timeout support for select arms

#![allow(dead_code)]

use std::collections::{HashMap, VecDeque};
use std::sync::{Arc, Mutex, Condvar};
use std::time::{Duration, Instant};

#[cfg(not(target_arch = "wasm32"))]
use std::thread;

use crate::env::Value;

// ── Task identifiers ────────────────────────────────────────

/// Unique task identifier (monotonically increasing).
pub type TaskId = u64;

// ── Task state machine ──────────────────────────────────────

/// State of a green thread task.
#[derive(Debug, Clone, PartialEq)]
pub enum TaskState {
    /// Ready to be scheduled.
    Ready,
    /// Currently executing on a worker thread.
    Running,
    /// Waiting on a future or channel.
    Waiting(WaitReason),
    /// Completed with a result value.
    Complete,
    /// Failed with an error message.
    Failed(String),
}

/// Reason a task is suspended.
#[derive(Debug, Clone, PartialEq)]
pub enum WaitReason {
    /// Waiting on a specific future to resolve.
    Future(TaskId),
    /// Waiting on a channel receive.
    Channel,
    /// Waiting on a timer (deadline in ms since scheduler start).
    Timer(u64),
}

// ── Future ──────────────────────────────────────────────────

/// A HEXA-level future: a shared slot that gets filled when a task completes.
#[derive(Clone)]
pub struct HexaFuture {
    /// The resolved value (None while pending).
    pub value: Arc<Mutex<Option<Value>>>,
    /// Condition variable to wake waiters.
    pub notify: Arc<Condvar>,
    /// The task that will produce this value.
    pub producer_task: TaskId,
}

impl HexaFuture {
    pub fn new(producer: TaskId) -> Self {
        Self {
            value: Arc::new(Mutex::new(None)),
            notify: Arc::new(Condvar::new()),
            producer_task: producer,
        }
    }

    /// Resolve the future with a value, waking all waiters.
    pub fn resolve(&self, val: Value) {
        let mut guard = self.value.lock().unwrap();
        *guard = Some(val);
        self.notify.notify_all();
    }

    /// Check if the future is resolved (non-blocking).
    pub fn poll(&self) -> Option<Value> {
        let guard = self.value.lock().unwrap();
        guard.clone()
    }

    /// Block until the future is resolved, with an optional timeout.
    pub fn wait(&self, timeout: Option<Duration>) -> Option<Value> {
        let guard = self.value.lock().unwrap();
        if guard.is_some() {
            return guard.clone();
        }
        let result = match timeout {
            Some(dur) => {
                let (guard, _timeout_result) = self.notify
                    .wait_timeout_while(guard, dur, |v| v.is_none())
                    .unwrap();
                guard.clone()
            }
            None => {
                let guard = self.notify
                    .wait_while(guard, |v| v.is_none())
                    .unwrap();
                guard.clone()
            }
        };
        result
    }
}

impl std::fmt::Debug for HexaFuture {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        let resolved = self.value.lock().unwrap().is_some();
        write!(f, "HexaFuture(task={}, resolved={})", self.producer_task, resolved)
    }
}

// ── Task ────────────────────────────────────────────────────

/// A green thread task: holds the computation's AST body, parameters,
/// captured environment, and execution state.
#[derive(Clone)]
pub struct Task {
    pub id: TaskId,
    pub state: TaskState,
    /// Function body (AST statements).
    pub body: Vec<crate::ast::Stmt>,
    /// Parameter bindings: (name, value).
    pub bindings: Vec<(String, Value)>,
    /// Captured environment from the spawning scope.
    pub captured_env: Vec<(String, Value)>,
    /// Struct/enum/method definitions cloned from the spawner.
    pub struct_defs: HashMap<String, Vec<(String, String, crate::ast::Visibility)>>,
    pub enum_defs: HashMap<String, Vec<(String, Option<Vec<String>>)>>,
    pub type_methods: HashMap<String, HashMap<String, (Vec<String>, Vec<crate::ast::Stmt>)>>,
    /// The future that this task will resolve upon completion.
    pub future: HexaFuture,
    /// Name for debugging.
    pub name: String,
}

// ── Scheduler ───────────────────────────────────────────────

/// Cooperative round-robin scheduler for green threads.
///
/// Tasks are executed on a small thread pool. The scheduler itself is
/// lock-free from the caller's perspective — `spawn_task` enqueues work,
/// `await_future` blocks the calling (HEXA) thread until a result appears.
pub struct Scheduler {
    /// Monotonically increasing task counter.
    next_id: Mutex<TaskId>,
    /// Task queue (tasks ready to run).
    ready_queue: Arc<Mutex<VecDeque<Task>>>,
    /// All known futures, indexed by producing task id.
    futures: Arc<Mutex<HashMap<TaskId, HexaFuture>>>,
    /// Worker thread handles.
    #[cfg(not(target_arch = "wasm32"))]
    workers: Mutex<Vec<thread::JoinHandle<()>>>,
    /// Scheduler start time (for timer calculations).
    start_time: Instant,
    /// Whether the scheduler has been started.
    started: Mutex<bool>,
    /// Signal to stop all workers.
    shutdown: Arc<Mutex<bool>>,
    /// Notify workers there is new work.
    work_available: Arc<Condvar>,
}

impl Scheduler {
    /// Create a new scheduler (does not start worker threads yet).
    pub fn new() -> Self {
        Self {
            next_id: Mutex::new(1),
            ready_queue: Arc::new(Mutex::new(VecDeque::new())),
            futures: Arc::new(Mutex::new(HashMap::new())),
            #[cfg(not(target_arch = "wasm32"))]
            workers: Mutex::new(Vec::new()),
            start_time: Instant::now(),
            started: Mutex::new(false),
            shutdown: Arc::new(Mutex::new(false)),
            work_available: Arc::new(Condvar::new()),
        }
    }

    /// Start the worker thread pool. Call once before spawning tasks.
    /// Uses `n_workers` OS threads to drive green thread execution.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn start(&self, n_workers: usize) {
        let mut started = self.started.lock().unwrap();
        if *started {
            return;
        }
        *started = true;

        let mut workers = self.workers.lock().unwrap();
        for i in 0..n_workers {
            let queue = Arc::clone(&self.ready_queue);
            let futures = Arc::clone(&self.futures);
            let shutdown = Arc::clone(&self.shutdown);
            let work_available = Arc::clone(&self.work_available);

            let handle = thread::Builder::new()
                .name(format!("hexa-worker-{}", i))
                .spawn(move || {
                    worker_loop(queue, futures, shutdown, work_available);
                })
                .expect("failed to spawn worker thread");
            workers.push(handle);
        }
    }

    /// Allocate a new task ID.
    fn next_task_id(&self) -> TaskId {
        let mut id = self.next_id.lock().unwrap();
        let current = *id;
        *id += 1;
        current
    }

    /// Spawn an async task. Returns a HexaFuture that will be resolved
    /// when the task completes.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn spawn_task(
        &self,
        name: String,
        body: Vec<crate::ast::Stmt>,
        bindings: Vec<(String, Value)>,
        captured_env: Vec<(String, Value)>,
        struct_defs: HashMap<String, Vec<(String, String, crate::ast::Visibility)>>,
        enum_defs: HashMap<String, Vec<(String, Option<Vec<String>>)>>,
        type_methods: HashMap<String, HashMap<String, (Vec<String>, Vec<crate::ast::Stmt>)>>,
    ) -> HexaFuture {
        // Ensure scheduler is running
        {
            let started = self.started.lock().unwrap();
            if !*started {
                drop(started);
                self.start(4); // default 4 workers
            }
        }

        let id = self.next_task_id();
        let future = HexaFuture::new(id);

        // Register the future
        {
            let mut futs = self.futures.lock().unwrap();
            futs.insert(id, future.clone());
        }

        let task = Task {
            id,
            state: TaskState::Ready,
            body,
            bindings,
            captured_env,
            struct_defs,
            enum_defs,
            type_methods,
            future: future.clone(),
            name,
        };

        // Enqueue the task
        {
            let mut queue = self.ready_queue.lock().unwrap();
            queue.push_back(task);
        }
        self.work_available.notify_one();

        future
    }

    /// Await a future: block until it resolves or timeout expires.
    /// Returns None on timeout.
    pub fn await_future(&self, future: &HexaFuture, timeout: Option<Duration>) -> Option<Value> {
        future.wait(timeout)
    }

    /// Poll a future without blocking. Returns Some(value) if resolved.
    pub fn poll_future(&self, future: &HexaFuture) -> Option<Value> {
        future.poll()
    }

    /// Select: wait for the first of several events to fire.
    /// Each event is a `SelectEvent`. Returns the index of the winning event
    /// and its value.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn select(&self, events: &[SelectEvent]) -> (usize, Value) {
        let deadline = Instant::now() + Duration::from_secs(30); // max 30s
        loop {
            for (i, event) in events.iter().enumerate() {
                match event {
                    SelectEvent::Channel(rx) => {
                        let rx_guard = rx.lock().unwrap();
                        match rx_guard.try_recv() {
                            Ok(val) => return (i, val),
                            Err(_) => continue,
                        }
                    }
                    SelectEvent::Future(fut) => {
                        if let Some(val) = fut.poll() {
                            return (i, val);
                        }
                    }
                    SelectEvent::Timeout(ms) => {
                        // Check if timeout has elapsed since select started
                        let elapsed = Instant::now().duration_since(
                            deadline - Duration::from_secs(30)
                        );
                        if elapsed.as_millis() as u64 >= *ms {
                            return (i, Value::Void);
                        }
                    }
                }
            }
            // Cooperative yield: sleep briefly before re-polling
            std::thread::sleep(Duration::from_micros(100));
            if Instant::now() >= deadline {
                // Absolute timeout — return last timeout arm if any, else first arm
                for (i, event) in events.iter().enumerate() {
                    if matches!(event, SelectEvent::Timeout(_)) {
                        return (i, Value::Void);
                    }
                }
                return (0, Value::Error("select: timed out with no timeout arm".into()));
            }
        }
    }

    /// Create a timer that fires after `ms` milliseconds.
    pub fn timer(&self, ms: u64) -> HexaFuture {
        let id = self.next_task_id();
        let future = HexaFuture::new(id);
        let future_clone = future.clone();

        #[cfg(not(target_arch = "wasm32"))]
        {
            thread::spawn(move || {
                thread::sleep(Duration::from_millis(ms));
                future_clone.resolve(Value::Void);
            });
        }

        future
    }

    /// Shut down all worker threads gracefully.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn shutdown(&self) {
        {
            let mut flag = self.shutdown.lock().unwrap();
            *flag = true;
        }
        self.work_available.notify_all();

        let mut workers = self.workers.lock().unwrap();
        for handle in workers.drain(..) {
            let _ = handle.join();
        }
    }

    /// Elapsed milliseconds since scheduler creation.
    pub fn elapsed_ms(&self) -> u64 {
        self.start_time.elapsed().as_millis() as u64
    }
}

impl Drop for Scheduler {
    fn drop(&mut self) {
        #[cfg(not(target_arch = "wasm32"))]
        {
            {
                let mut flag = self.shutdown.lock().unwrap();
                *flag = true;
            }
            self.work_available.notify_all();
            let mut workers = self.workers.lock().unwrap();
            for handle in workers.drain(..) {
                let _ = handle.join();
            }
        }
    }
}

// ── Task group (structured concurrency) ─────────────────────

/// A group of tasks that must all complete before a scope exits.
/// Implements structured concurrency: spawns inside a `scope` block
/// are tracked, and the scope blocks until all complete or one fails.
#[derive(Clone, Debug)]
pub struct TaskGroup {
    /// Futures for all tasks spawned within this scope.
    pub futures: Vec<HexaFuture>,
}

impl TaskGroup {
    /// Create a new empty task group.
    pub fn new() -> Self {
        Self { futures: Vec::new() }
    }

    /// Register a future (from a spawned task) with this group.
    pub fn add(&mut self, future: HexaFuture) {
        self.futures.push(future);
    }

    /// Wait for all tasks in the group to complete.
    /// Returns the collected results in spawn order.
    /// If any task returns an Error value, remaining tasks are cancelled
    /// (their results are ignored) and the error is propagated.
    pub fn wait_all(&self, timeout: Option<Duration>) -> Result<Vec<Value>, String> {
        let mut results = Vec::with_capacity(self.futures.len());
        for fut in &self.futures {
            match fut.wait(timeout) {
                Some(Value::Error(e)) => {
                    return Err(e);
                }
                Some(val) => results.push(val),
                None => {
                    return Err("scope: task timed out".into());
                }
            }
        }
        Ok(results)
    }

    /// Cancel all tasks in the group (best-effort).
    /// Since green threads run to completion on worker threads, "cancel" means
    /// we stop waiting and discard results.
    pub fn cancel(&self) {
        // In this cooperative scheduler, tasks run to completion.
        // Cancellation means we don't wait for or use their results.
    }
}

// ── Select events ───────────────────────────────────────────

/// An event that a `select` statement can wait on.
pub enum SelectEvent {
    /// Wait for a value from a channel receiver.
    #[cfg(not(target_arch = "wasm32"))]
    Channel(Arc<Mutex<std::sync::mpsc::Receiver<Value>>>),
    /// Wait for a future to resolve.
    Future(HexaFuture),
    /// Wait for a timeout (milliseconds).
    Timeout(u64),
}

// ── Worker loop ─────────────────────────────────────────────

/// Main loop for a worker thread: dequeue tasks and execute them.
#[cfg(not(target_arch = "wasm32"))]
fn worker_loop(
    queue: Arc<Mutex<VecDeque<Task>>>,
    _futures: Arc<Mutex<HashMap<TaskId, HexaFuture>>>,
    shutdown: Arc<Mutex<bool>>,
    work_available: Arc<Condvar>,
) {
    loop {
        // Check shutdown
        if *shutdown.lock().unwrap() {
            return;
        }

        // Try to dequeue a task
        let task = {
            let mut q = queue.lock().unwrap();
            if q.is_empty() {
                // Wait for work with a timeout
                let q = work_available.wait_timeout(q, Duration::from_millis(50)).unwrap().0;
                // Re-check after wait
                drop(q);
                let mut q2 = queue.lock().unwrap();
                q2.pop_front()
            } else {
                q.pop_front()
            }
        };

        if *shutdown.lock().unwrap() {
            return;
        }

        if let Some(task) = task {
            execute_task(task);
        }
    }
}

/// Execute a single task to completion.
#[cfg(not(target_arch = "wasm32"))]
fn execute_task(task: Task) {
    let mut interp = crate::interpreter::Interpreter::new();
    interp.struct_defs = task.struct_defs;
    interp.enum_defs = task.enum_defs;
    interp.type_methods = task.type_methods;

    // Restore captured environment
    for (name, val) in &task.captured_env {
        interp.env.define(name, val.clone());
    }

    // Bind parameters
    interp.env.push_scope();
    for (name, val) in &task.bindings {
        interp.env.define(name, val.clone());
    }

    let mut result = Value::Void;
    for stmt in &task.body {
        match interp.exec_stmt(stmt) {
            Ok(v) => {
                result = v;
                if interp.return_value.is_some() {
                    result = interp.return_value.take().unwrap();
                    break;
                }
            }
            Err(e) => {
                result = Value::Error(e.message);
                break;
            }
        }
    }
    interp.env.pop_scope();

    // Resolve the future with the task's result
    task.future.resolve(result);
}

// ── Global scheduler (lazy singleton) ───────────────────────

use std::sync::OnceLock;

static GLOBAL_SCHEDULER: OnceLock<Scheduler> = OnceLock::new();

/// Get or initialize the global scheduler.
pub fn global_scheduler() -> &'static Scheduler {
    GLOBAL_SCHEDULER.get_or_init(|| {
        let s = Scheduler::new();
        #[cfg(not(target_arch = "wasm32"))]
        s.start(4);
        s
    })
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_future_resolve_and_poll() {
        let fut = HexaFuture::new(1);
        assert!(fut.poll().is_none());
        fut.resolve(Value::Int(42));
        assert_eq!(fut.poll().unwrap().to_string(), "42");
    }

    #[test]
    fn test_future_wait_with_timeout() {
        let fut = HexaFuture::new(2);
        let fut_clone = fut.clone();
        std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(10));
            fut_clone.resolve(Value::Str("done".into()));
        });
        let val = fut.wait(Some(Duration::from_secs(1)));
        assert!(val.is_some());
        assert_eq!(val.unwrap().to_string(), "done");
    }

    #[test]
    fn test_future_timeout_expires() {
        let fut = HexaFuture::new(3);
        // Don't resolve — should timeout
        let val = fut.wait(Some(Duration::from_millis(10)));
        assert!(val.is_none());
    }

    #[test]
    fn test_scheduler_spawn_and_await() {
        let sched = Scheduler::new();
        sched.start(2);

        let body = vec![
            crate::ast::Stmt::Return(Some(crate::ast::Expr::IntLit(99))),
        ];
        let future = sched.spawn_task(
            "test".into(),
            body,
            vec![],
            vec![],
            HashMap::new(),
            HashMap::new(),
            HashMap::new(),
        );

        let result = sched.await_future(&future, Some(Duration::from_secs(2)));
        assert!(result.is_some());
        assert_eq!(result.unwrap().to_string(), "99");

        sched.shutdown();
    }

    #[test]
    fn test_timer() {
        let sched = Scheduler::new();
        let timer_fut = sched.timer(20);
        let val = sched.await_future(&timer_fut, Some(Duration::from_secs(1)));
        assert!(val.is_some());
    }

    #[test]
    fn test_multiple_tasks() {
        let sched = Scheduler::new();
        sched.start(2);

        let f1 = sched.spawn_task(
            "t1".into(),
            vec![crate::ast::Stmt::Return(Some(crate::ast::Expr::IntLit(1)))],
            vec![], vec![], HashMap::new(), HashMap::new(), HashMap::new(),
        );
        let f2 = sched.spawn_task(
            "t2".into(),
            vec![crate::ast::Stmt::Return(Some(crate::ast::Expr::IntLit(2)))],
            vec![], vec![], HashMap::new(), HashMap::new(), HashMap::new(),
        );
        let f3 = sched.spawn_task(
            "t3".into(),
            vec![crate::ast::Stmt::Return(Some(crate::ast::Expr::IntLit(3)))],
            vec![], vec![], HashMap::new(), HashMap::new(), HashMap::new(),
        );

        let v1 = sched.await_future(&f1, Some(Duration::from_secs(2)));
        let v2 = sched.await_future(&f2, Some(Duration::from_secs(2)));
        let v3 = sched.await_future(&f3, Some(Duration::from_secs(2)));

        assert_eq!(v1.unwrap().to_string(), "1");
        assert_eq!(v2.unwrap().to_string(), "2");
        assert_eq!(v3.unwrap().to_string(), "3");

        sched.shutdown();
    }
}
