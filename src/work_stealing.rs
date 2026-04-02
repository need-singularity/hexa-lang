//! Work-stealing scheduler for HEXA-LANG async runtime.
//!
//! Implements an M:N threading model where M green threads are multiplexed
//! onto N OS threads. Each worker thread has a local deque; when it runs
//! out of work it steals from other workers' deques.
//!
//! Architecture:
//!   - WorkerDeque: per-thread double-ended queue (push/pop local, steal remote)
//!   - WorkStealingScheduler: manages N workers, each with a deque
//!   - Tasks are submitted to the least-loaded worker (or current worker)
//!   - Idle workers steal from the busiest worker's deque

#![allow(dead_code)]

use std::collections::VecDeque;
use std::sync::{Arc, Mutex, Condvar};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};

#[cfg(not(target_arch = "wasm32"))]
use std::thread;

use crate::env::Value;
use crate::async_runtime::{HexaFuture, TaskId};

// ── Work item ──────────────────────────────────────────────────

/// A unit of work that can be executed by a worker thread.
#[derive(Clone)]
pub struct WorkItem {
    pub id: TaskId,
    pub body: Vec<crate::ast::Stmt>,
    pub bindings: Vec<(String, Value)>,
    pub captured_env: Vec<(String, Value)>,
    pub struct_defs: std::collections::HashMap<String, Vec<(String, String, crate::ast::Visibility)>>,
    pub enum_defs: std::collections::HashMap<String, Vec<(String, Option<Vec<String>>)>>,
    pub type_methods: std::collections::HashMap<String, std::collections::HashMap<String, (Vec<String>, Vec<crate::ast::Stmt>)>>,
    pub future: HexaFuture,
    pub name: String,
}

// ── Per-worker deque ───────────────────────────────────────────

/// A thread-safe double-ended queue for work items.
/// The owning worker pushes/pops from the back (LIFO for locality).
/// Stealers take from the front (FIFO for fairness).
#[derive(Clone)]
pub struct WorkerDeque {
    inner: Arc<Mutex<VecDeque<WorkItem>>>,
}

impl WorkerDeque {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(VecDeque::new())),
        }
    }

    /// Push a work item onto the back (local worker side).
    pub fn push(&self, item: WorkItem) {
        self.inner.lock().unwrap().push_back(item);
    }

    /// Pop a work item from the back (local worker side, LIFO).
    pub fn pop(&self) -> Option<WorkItem> {
        self.inner.lock().unwrap().pop_back()
    }

    /// Steal a work item from the front (remote worker side, FIFO).
    pub fn steal(&self) -> Option<WorkItem> {
        self.inner.lock().unwrap().pop_front()
    }

    /// Number of items in the deque.
    pub fn len(&self) -> usize {
        self.inner.lock().unwrap().len()
    }

    /// Whether the deque is empty.
    pub fn is_empty(&self) -> bool {
        self.inner.lock().unwrap().is_empty()
    }
}

// ── Work-stealing scheduler ────────────────────────────────────

/// M:N work-stealing scheduler.
/// Distributes tasks across N OS worker threads, each with a local deque.
pub struct WorkStealingScheduler {
    /// Per-worker deques.
    deques: Vec<WorkerDeque>,
    /// Number of workers.
    num_workers: usize,
    /// Next task ID counter.
    next_id: AtomicU64,
    /// Shutdown signal.
    shutdown: Arc<AtomicBool>,
    /// Condition variable to wake idle workers.
    work_signal: Arc<Condvar>,
    /// Mutex paired with work_signal.
    work_mutex: Arc<Mutex<bool>>,
    /// Worker thread handles.
    #[cfg(not(target_arch = "wasm32"))]
    handles: Mutex<Vec<thread::JoinHandle<()>>>,
    /// Whether the scheduler has been started.
    started: AtomicBool,
    /// Total tasks completed (for stats).
    tasks_completed: AtomicU64,
    /// Total steals performed (for stats).
    steals_performed: AtomicU64,
}

impl WorkStealingScheduler {
    /// Create a new scheduler with `n` workers. Does not start threads yet.
    pub fn new(n: usize) -> Self {
        let n = n.max(1);
        let deques: Vec<WorkerDeque> = (0..n).map(|_| WorkerDeque::new()).collect();
        Self {
            deques,
            num_workers: n,
            next_id: AtomicU64::new(1),
            shutdown: Arc::new(AtomicBool::new(false)),
            work_signal: Arc::new(Condvar::new()),
            work_mutex: Arc::new(Mutex::new(false)),
            #[cfg(not(target_arch = "wasm32"))]
            handles: Mutex::new(Vec::new()),
            started: AtomicBool::new(false),
            tasks_completed: AtomicU64::new(0),
            steals_performed: AtomicU64::new(0),
        }
    }

    /// Start the worker threads.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn start(&self) {
        if self.started.swap(true, Ordering::SeqCst) {
            return; // already started
        }

        let mut handles = self.handles.lock().unwrap();
        for worker_idx in 0..self.num_workers {
            let deques: Vec<WorkerDeque> = self.deques.clone();
            let shutdown = Arc::clone(&self.shutdown);
            let work_signal = Arc::clone(&self.work_signal);
            let work_mutex = Arc::clone(&self.work_mutex);
            let tasks_completed = &self.tasks_completed as *const AtomicU64 as usize;
            let steals_performed = &self.steals_performed as *const AtomicU64 as usize;

            let handle = thread::Builder::new()
                .name(format!("hexa-ws-worker-{}", worker_idx))
                .spawn(move || {
                    let tasks_completed = unsafe { &*(tasks_completed as *const AtomicU64) };
                    let steals_performed = unsafe { &*(steals_performed as *const AtomicU64) };
                    worker_loop(
                        worker_idx,
                        &deques,
                        shutdown,
                        work_signal,
                        work_mutex,
                        tasks_completed,
                        steals_performed,
                    );
                })
                .expect("failed to spawn work-stealing worker");
            handles.push(handle);
        }
    }

    /// Allocate a new task ID.
    pub fn next_task_id(&self) -> TaskId {
        self.next_id.fetch_add(1, Ordering::Relaxed)
    }

    /// Submit a work item to the least-loaded worker.
    pub fn submit(&self, item: WorkItem) {
        // Find the worker with the fewest items
        let mut min_idx = 0;
        let mut min_len = usize::MAX;
        for (i, deque) in self.deques.iter().enumerate() {
            let len = deque.len();
            if len < min_len {
                min_len = len;
                min_idx = i;
            }
        }
        self.deques[min_idx].push(item);
        // Wake a sleeping worker
        self.work_signal.notify_one();
    }

    /// Spawn a task and return a future for its result.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn spawn(
        &self,
        name: String,
        body: Vec<crate::ast::Stmt>,
        bindings: Vec<(String, Value)>,
        captured_env: Vec<(String, Value)>,
        struct_defs: std::collections::HashMap<String, Vec<(String, String, crate::ast::Visibility)>>,
        enum_defs: std::collections::HashMap<String, Vec<(String, Option<Vec<String>>)>>,
        type_methods: std::collections::HashMap<String, std::collections::HashMap<String, (Vec<String>, Vec<crate::ast::Stmt>)>>,
    ) -> HexaFuture {
        if !self.started.load(Ordering::SeqCst) {
            self.start();
        }

        let id = self.next_task_id();
        let future = HexaFuture::new(id);

        let item = WorkItem {
            id,
            body,
            bindings,
            captured_env,
            struct_defs,
            enum_defs,
            type_methods,
            future: future.clone(),
            name,
        };

        self.submit(item);
        future
    }

    /// Get scheduler statistics.
    pub fn stats(&self) -> SchedulerStats {
        let mut queue_sizes = Vec::new();
        for deque in &self.deques {
            queue_sizes.push(deque.len());
        }
        SchedulerStats {
            num_workers: self.num_workers,
            tasks_completed: self.tasks_completed.load(Ordering::Relaxed),
            steals_performed: self.steals_performed.load(Ordering::Relaxed),
            queue_sizes,
        }
    }

    /// Shut down all workers.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn shutdown(&self) {
        self.shutdown.store(true, Ordering::SeqCst);
        // Wake all workers so they see the shutdown flag
        self.work_signal.notify_all();

        let mut handles = self.handles.lock().unwrap();
        for handle in handles.drain(..) {
            let _ = handle.join();
        }
    }
}

#[cfg(not(target_arch = "wasm32"))]
impl Drop for WorkStealingScheduler {
    fn drop(&mut self) {
        self.shutdown.store(true, Ordering::SeqCst);
        self.work_signal.notify_all();
        let mut handles = self.handles.lock().unwrap();
        for handle in handles.drain(..) {
            let _ = handle.join();
        }
    }
}

/// Scheduler statistics.
#[derive(Debug, Clone)]
pub struct SchedulerStats {
    pub num_workers: usize,
    pub tasks_completed: u64,
    pub steals_performed: u64,
    pub queue_sizes: Vec<usize>,
}

// ── Worker loop ────────────────────────────────────────────────

#[cfg(not(target_arch = "wasm32"))]
fn worker_loop(
    my_idx: usize,
    deques: &[WorkerDeque],
    shutdown: Arc<AtomicBool>,
    work_signal: Arc<Condvar>,
    work_mutex: Arc<Mutex<bool>>,
    tasks_completed: &AtomicU64,
    steals_performed: &AtomicU64,
) {
    loop {
        if shutdown.load(Ordering::Relaxed) {
            return;
        }

        // Try to pop from own deque first
        if let Some(item) = deques[my_idx].pop() {
            execute_work_item(item);
            tasks_completed.fetch_add(1, Ordering::Relaxed);
            continue;
        }

        // Try to steal from other workers
        let mut stolen = false;
        for i in 0..deques.len() {
            if i == my_idx { continue; }
            if let Some(item) = deques[i].steal() {
                steals_performed.fetch_add(1, Ordering::Relaxed);
                execute_work_item(item);
                tasks_completed.fetch_add(1, Ordering::Relaxed);
                stolen = true;
                break;
            }
        }

        if stolen { continue; }

        // No work available — wait for signal
        if shutdown.load(Ordering::Relaxed) {
            return;
        }

        let guard = work_mutex.lock().unwrap();
        let _ = work_signal.wait_timeout(guard, std::time::Duration::from_millis(50)).unwrap();
    }
}

/// Execute a single work item.
#[cfg(not(target_arch = "wasm32"))]
fn execute_work_item(item: WorkItem) {
    let mut interp = crate::interpreter::Interpreter::new();
    interp.struct_defs = item.struct_defs;
    interp.enum_defs = item.enum_defs;
    interp.type_methods = item.type_methods;

    for (name, val) in &item.captured_env {
        interp.env.define(name, val.clone());
    }

    interp.env.push_scope();
    for (name, val) in &item.bindings {
        interp.env.define(name, val.clone());
    }

    let mut result = Value::Void;
    for stmt in &item.body {
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

    item.future.resolve(result);
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_worker_deque_push_pop() {
        let deque = WorkerDeque::new();
        assert!(deque.is_empty());

        let future = HexaFuture::new(1);
        let item = WorkItem {
            id: 1,
            body: vec![],
            bindings: vec![],
            captured_env: vec![],
            struct_defs: std::collections::HashMap::new(),
            enum_defs: std::collections::HashMap::new(),
            type_methods: std::collections::HashMap::new(),
            future,
            name: "test".into(),
        };
        deque.push(item);
        assert_eq!(deque.len(), 1);

        let popped = deque.pop();
        assert!(popped.is_some());
        assert!(deque.is_empty());
    }

    #[test]
    fn test_worker_deque_steal() {
        let deque = WorkerDeque::new();
        for i in 0..3 {
            let future = HexaFuture::new(i);
            deque.push(WorkItem {
                id: i,
                body: vec![],
                bindings: vec![],
                captured_env: vec![],
                struct_defs: std::collections::HashMap::new(),
                enum_defs: std::collections::HashMap::new(),
                type_methods: std::collections::HashMap::new(),
                future,
                name: format!("task-{}", i),
            });
        }

        // Steal takes from front (FIFO)
        let stolen = deque.steal().unwrap();
        assert_eq!(stolen.id, 0);

        // Pop takes from back (LIFO)
        let popped = deque.pop().unwrap();
        assert_eq!(popped.id, 2);

        assert_eq!(deque.len(), 1);
    }

    #[test]
    fn test_scheduler_stats() {
        let sched = WorkStealingScheduler::new(4);
        let stats = sched.stats();
        assert_eq!(stats.num_workers, 4);
        assert_eq!(stats.tasks_completed, 0);
        assert_eq!(stats.steals_performed, 0);
        assert_eq!(stats.queue_sizes.len(), 4);
    }

    #[test]
    fn test_scheduler_submit_least_loaded() {
        let sched = WorkStealingScheduler::new(2);
        // Submit 3 items — they should go to least-loaded worker
        for i in 0..3 {
            let future = HexaFuture::new(i);
            sched.submit(WorkItem {
                id: i,
                body: vec![],
                bindings: vec![],
                captured_env: vec![],
                struct_defs: std::collections::HashMap::new(),
                enum_defs: std::collections::HashMap::new(),
                type_methods: std::collections::HashMap::new(),
                future,
                name: format!("task-{}", i),
            });
        }
        let stats = sched.stats();
        let total: usize = stats.queue_sizes.iter().sum();
        assert_eq!(total, 3);
        // Items should be roughly balanced
        assert!(stats.queue_sizes[0] <= 2);
        assert!(stats.queue_sizes[1] <= 2);
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_scheduler_spawn_and_resolve() {
        let sched = WorkStealingScheduler::new(2);
        sched.start();

        let future = sched.spawn(
            "test_task".into(),
            vec![crate::ast::Stmt::Return(Some(crate::ast::Expr::IntLit(42)))],
            vec![],
            vec![],
            std::collections::HashMap::new(),
            std::collections::HashMap::new(),
            std::collections::HashMap::new(),
        );

        let result = future.wait(Some(std::time::Duration::from_secs(5)));
        assert!(result.is_some());
        assert_eq!(result.unwrap().to_string(), "42");

        sched.shutdown();
    }

    #[test]
    fn test_task_id_counter() {
        let sched = WorkStealingScheduler::new(1);
        let id1 = sched.next_task_id();
        let id2 = sched.next_task_id();
        assert_eq!(id2, id1 + 1);
    }
}
