// HEXA Playground — Editor + WASM bridge
// Loads the WASM module and provides run/format/lint functionality.

// ── Examples ────────────────────────────────────────────────────────────

const EXAMPLES = {
    "hello": {
        name: "Hello World (n=6)",
        code: `println("=== HEXA-LANG: The Perfect Number Language ===")
println("")
println("n=6 Constants:")
println("  sigma(6) =", sigma(6))
println("  phi(6)   =", phi(6))
println("  tau(6)   =", tau(6))
println("  J2(6)    =", sigma(6) * phi(6))
println("")
println("The identity: sigma(6) * phi(6) = 6 * tau(6)")
println("  ", sigma(6) * phi(6), "=", 6 * tau(6))
println("  Perfect? ", sigma(6) * phi(6) == 6 * tau(6))`
    },

    "fibonacci": {
        name: "Fibonacci",
        code: `fn fib(n: int) -> int {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

// sigma(6) = 12: print first 12 Fibonacci numbers
println("Fibonacci sequence (first sigma(6)=12 terms):")
for i in 0..12 {
    println("  fib(", i, ") =", fib(i))
}`
    },

    "pattern": {
        name: "Pattern Matching",
        code: `println("=== Pattern Matching + Control Flow ===")

fn fizzbuzz(n: int) -> string {
    if n % 6 == 0 {
        return "FizzBuzz (n=6!)"
    }
    if n % 3 == 0 {
        return "Fizz"
    }
    if n % 2 == 0 {
        return "Buzz"
    }
    return "."
}

println("FizzBuzz with n=6 alignment:")
for i in 1..25 {
    println("  ", i, ":", fizzbuzz(i))
}
println("Note: every 6th number = FizzBuzz (n=6)")`
    },

    "consciousness": {
        name: "Consciousness DSL",
        code: `// Consciousness experiment with intent/verify blocks

intent "Can Phi survive compression?" {
    hypothesis: "Phi scales with cell count"
    cells: 64
    steps: 300
    topology: "ring"
}

let experiment = __intent__
println("Experiment:", experiment.description)

// Psi constants from n=6
println("")
println("Psi-Constants (derived from n=6):")
println("  alpha    =", psi_coupling())
println("  balance  =", psi_balance())
println("  steps    =", psi_steps())
println("  entropy  =", psi_entropy())

// Verify consciousness architecture
verify "hexad_integrity" {
    let all = hexad_modules()
    let right = hexad_right()
    let left = hexad_left()
    assert len(all) == 6
    assert len(right) == 3
    assert len(left) == 3
}

println("")
println("All verifications passed.")`
    },

    "egyptian": {
        name: "Egyptian Fractions",
        code: `println("=== Egyptian Fraction Memory Model ===")
println("1/2 + 1/3 + 1/6 = 1 (Perfect Number 6)")
println("")

let half = 1.0 / 2.0
let third = 1.0 / 3.0
let sixth = 1.0 / 6.0
let total = half + third + sixth

println("  Stack:  1/2 =", half)
println("  Heap:   1/3 =", third)
println("  Arena:  1/6 =", sixth)
println("  Total:      =", total)

let mut diff = total - 1.0
if diff < 0.0 {
    diff = 0.0 - diff
}
assert diff < 0.0001
println("  Error:      <", 0.0001)
println("")

// Egyptian fraction decomposition
fn egyptian(num: int, den: int) -> string {
    if num == 0 {
        return "0"
    }
    let mut n = num
    let mut d = den
    let mut result = ""
    while n > 0 {
        let unit_den = (d + n - 1) / n
        if result != "" {
            result = result + " + "
        }
        result = result + "1/" + to_string(unit_den)
        n = n * unit_den - d
        d = d * unit_den
    }
    return result
}

println("Egyptian fraction decompositions:")
for i in 2..13 {
    println("  1/" + to_string(i), "→", egyptian(1, i))
}`
    },

    "structs": {
        name: "Structs + Closures",
        code: `// Structs and closures in HEXA

struct Point {
    x: float
    y: float
}

impl Point {
    fn distance(self, other: Point) -> float {
        let dx = self.x - other.x
        let dy = self.y - other.y
        return sqrt(dx * dx + dy * dy)
    }

    fn scale(self, factor: float) -> Point {
        return Point { x: self.x * factor, y: self.y * factor }
    }
}

let origin = Point { x: 0.0, y: 0.0 }
let p = Point { x: 3.0, y: 4.0 }

println("Distance from origin to (3,4):", origin.distance(p))
println("Scaled by phi(6)=2:", p.scale(2.0))

// Closures
let make_adder = fn(n: int) -> fn {
    return fn(x: int) -> int {
        return x + n
    }
}

let add6 = make_adder(6)
let add12 = make_adder(12)

println("")
println("Closures:")
println("  add6(10) =", add6(10))
println("  add12(10) =", add12(10))

// Higher-order: map over array
let nums = [1, 2, 3, 4, 5, 6]
let mut doubled = []
for n in nums {
    push(doubled, n * 2)
}
println("  doubled:", doubled)`
    },

    "loops": {
        name: "Break + Continue",
        code: `// break/continue in loops
println("=== Prime Finder (break/continue) ===")

fn is_prime(n: int) -> bool {
    if n < 2 { return false }
    let i = 2
    while i * i <= n {
        if n % i == 0 { return false }
        i = i + 1
    }
    true
}

// Find first 6 primes (n=6!)
let count = 0
let n = 2
let primes = []
loop {
    if count >= 6 { break }
    if !is_prime(n) {
        n = n + 1
        continue
    }
    push(primes, n)
    count = count + 1
    n = n + 1
}
println("First sigma(6)=6 primes:", primes)

// Sum only odd numbers using continue
let sum = 0
for i in 0..20 {
    if i % 2 == 0 { continue }
    sum = sum + i
}
println("Sum of odd 0..20:", sum)`
    },

    "benchmark": {
        name: "Benchmark (333x JIT)",
        code: `// Performance benchmark — runs at JIT speed!
fn sigma(n: int) -> int {
    let sum = 0
    let i = 1
    while i <= n {
        if n % i == 0 { sum = sum + i }
        i = i + 1
    }
    sum
}

fn fib(n: int) -> int {
    let a = 0
    let b = 1
    let i = 0
    while i < n {
        let t = a + b
        a = b
        b = t
        i = i + 1
    }
    a
}

let total = 0
for i in 0..10000 {
    total = total + sigma(6) + fib(20)
}
println("Result:", total)
println("sigma(6)=12, fib(20)=6765")
println("Expected: (12+6765)*10000 =", (12+6765)*10000)`
    }
};

// ── WASM Module ─────────────────────────────────────────────────────────

let wasmModule = null;

async function initWasm() {
    try {
        const mod = await import('./pkg/hexa_lang.js');
        await mod.default();
        wasmModule = mod;
        setStatus('ready', `HEXA v1.0.0 (WASM)`);
        return true;
    } catch (e) {
        console.error('WASM init failed:', e);
        setStatus('error', 'WASM load failed - see console');
        return false;
    }
}

// ── DOM refs ────────────────────────────────────────────────────────────

const editor = document.getElementById('editor');
const highlight = document.getElementById('highlight');
const lineNumbers = document.getElementById('line-numbers');
const output = document.getElementById('output');
const lintPanel = document.getElementById('lint-panel');
const statusEl = document.getElementById('status');
const exampleSelect = document.getElementById('example-select');

// ── Syntax highlighting ─────────────────────────────────────────────────

const KEYWORDS = new Set([
    'if', 'else', 'match', 'for', 'while', 'loop',
    'type', 'struct', 'enum', 'trait', 'impl',
    'fn', 'return', 'yield', 'async', 'await',
    'let', 'mut', 'const', 'static',
    'mod', 'use', 'pub', 'crate',
    'own', 'borrow', 'move', 'drop',
    'spawn', 'channel', 'select', 'atomic',
    'effect', 'handle', 'resume', 'pure',
    'proof', 'assert', 'invariant', 'theorem',
    'macro', 'derive', 'where', 'comptime',
    'try', 'catch', 'throw', 'panic', 'recover',
    'intent', 'generate', 'verify', 'optimize',
    'in',
]);

const BUILTINS = new Set([
    'println', 'print', 'len', 'push', 'pop', 'to_string', 'to_int',
    'to_float', 'sqrt', 'abs', 'min', 'max', 'gcd', 'pow',
    'sigma', 'phi', 'tau', 'sopfr', 'mobius',
    'psi_coupling', 'psi_balance', 'psi_steps', 'psi_entropy', 'psi_frustration',
    'hexad_modules', 'hexad_right', 'hexad_left',
    'consciousness_max_cells', 'consciousness_decoder_dim', 'consciousness_phi',
    'read_file', 'write_file', 'channel', 'exit', 'clock',
    'format', 'split', 'trim', 'contains', 'replace', 'chars',
    'keys', 'values', 'sort', 'reverse', 'join', 'map', 'filter',
    'Some', 'Ok', 'Err',
]);

const TYPES = new Set([
    'int', 'float', 'string', 'bool', 'char', 'byte', 'void',
    'array', 'map', 'set', 'fn',
]);

function escapeHtml(text) {
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
}

function highlightCode(source) {
    // Tokenize with regex, preserving order
    const tokens = [];
    const re = /\/\/[^\n]*|"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)'|\b\d+(?:\.\d+)?\b|\b[a-zA-Z_][a-zA-Z0-9_]*\b|[^\s]/g;
    let lastIndex = 0;
    let match;

    while ((match = re.exec(source)) !== null) {
        // Preserve whitespace between tokens
        if (match.index > lastIndex) {
            tokens.push({ type: 'ws', text: source.slice(lastIndex, match.index) });
        }

        const text = match[0];
        let type = 'plain';

        if (text.startsWith('//')) {
            type = 'comment';
        } else if (text.startsWith('"') || text.startsWith("'")) {
            type = 'string';
        } else if (/^\d/.test(text)) {
            // Check for n=6 special numbers
            if (['6', '12', '24', '720', '1024', '384', '768'].includes(text)) {
                type = 'n6';
            } else {
                type = 'number';
            }
        } else if (text === 'true' || text === 'false') {
            type = 'bool';
        } else if (KEYWORDS.has(text)) {
            type = 'keyword';
        } else if (BUILTINS.has(text)) {
            type = 'builtin';
        } else if (TYPES.has(text)) {
            type = 'type';
        }

        tokens.push({ type, text });
        lastIndex = re.lastIndex;
    }

    // Trailing whitespace
    if (lastIndex < source.length) {
        tokens.push({ type: 'ws', text: source.slice(lastIndex) });
    }

    // Build HTML
    return tokens.map(t => {
        const escaped = escapeHtml(t.text);
        switch (t.type) {
            case 'keyword':  return `<span class="hl-keyword">${escaped}</span>`;
            case 'builtin':  return `<span class="hl-builtin">${escaped}</span>`;
            case 'string':   return `<span class="hl-string">${escaped}</span>`;
            case 'number':   return `<span class="hl-number">${escaped}</span>`;
            case 'n6':       return `<span class="hl-n6">${escaped}</span>`;
            case 'comment':  return `<span class="hl-comment">${escaped}</span>`;
            case 'type':     return `<span class="hl-type">${escaped}</span>`;
            case 'bool':     return `<span class="hl-bool">${escaped}</span>`;
            default:         return escaped;
        }
    }).join('');
}

// ── Line numbers ────────────────────────────────────────────────────────

function updateLineNumbers() {
    const lines = editor.value.split('\n');
    lineNumbers.textContent = lines.map((_, i) => i + 1).join('\n');
}

// ── Sync scroll ─────────────────────────────────────────────────────────

function syncScroll() {
    highlight.scrollTop = editor.scrollTop;
    highlight.scrollLeft = editor.scrollLeft;
    lineNumbers.style.transform = `translateY(-${editor.scrollTop}px)`;
}

// ── Update highlight ────────────────────────────────────────────────────

function updateHighlight() {
    highlight.innerHTML = highlightCode(editor.value);
    updateLineNumbers();
}

// ── Status ──────────────────────────────────────────────────────────────

function setStatus(cls, text) {
    statusEl.className = 'status ' + cls;
    statusEl.textContent = text;
}

// ── Run ─────────────────────────────────────────────────────────────────

function runCode() {
    if (!wasmModule) {
        output.innerHTML = '<span class="error-line">WASM module not loaded. Run playground/build.hexa first.</span>';
        return;
    }

    const source = editor.value;
    setStatus('running', 'Running...');
    output.textContent = '';

    // Use setTimeout to let the UI update before blocking on WASM
    setTimeout(() => {
        try {
            const resultJson = wasmModule.run_hexa(source);
            const result = JSON.parse(resultJson);

            let html = '';
            if (result.output) {
                html += escapeHtml(result.output);
            }
            const tierLabel = result.tier === 'vm' ? 'VM' : 'Interpreter';
            if (result.error) {
                if (html) html += '\n';
                html += `<span class="error-line">${escapeHtml(result.error)}</span>`;
                setStatus('error', `Error [${tierLabel}]`);
            } else {
                setStatus('success', `Done via ${tierLabel} (${new Date().toLocaleTimeString()})`);
            }
            output.innerHTML = html || '<span class="info-line">(no output)</span>';
        } catch (e) {
            output.innerHTML = `<span class="error-line">Internal error: ${escapeHtml(e.message)}</span>`;
            setStatus('error', 'Internal error');
        }
    }, 10);
}

// ── Format ──────────────────────────────────────────────────────────────

function formatCode() {
    if (!wasmModule) return;

    try {
        const resultJson = wasmModule.format_hexa(editor.value);
        const result = JSON.parse(resultJson);

        if (result.error) {
            setStatus('error', 'Format error');
        } else {
            editor.value = result.formatted;
            updateHighlight();
            setStatus('success', 'Formatted');
        }
    } catch (e) {
        setStatus('error', 'Format failed');
    }
}

// ── Lint ─────────────────────────────────────────────────────────────────

let lintTimer = null;

function lintCode() {
    if (!wasmModule) return;

    try {
        const resultJson = wasmModule.lint_hexa(editor.value);
        const result = JSON.parse(resultJson);

        lintPanel.innerHTML = '';
        const countEl = document.getElementById('lint-count');

        if (result.error) {
            countEl.textContent = '';
            return;
        }

        if (result.warnings.length === 0) {
            countEl.textContent = '';
            return;
        }

        countEl.textContent = `${result.warnings.length} warning${result.warnings.length > 1 ? 's' : ''}`;

        result.warnings.forEach(w => {
            const item = document.createElement('div');
            item.className = 'lint-item';
            item.innerHTML = `
                <span class="lint-level ${w.level}">${w.level}</span>
                <span class="lint-line">${w.line > 0 ? 'L' + w.line : ''}</span>
                <span class="lint-msg">${escapeHtml(w.message)}</span>
            `;
            // Click to jump to line
            if (w.line > 0) {
                item.style.cursor = 'pointer';
                item.addEventListener('click', () => jumpToLine(w.line));
            }
            lintPanel.appendChild(item);
        });
    } catch (e) {
        // Silently ignore lint errors during typing
    }
}

function scheduleLint() {
    if (lintTimer) clearTimeout(lintTimer);
    lintTimer = setTimeout(lintCode, 500);
}

// ── Jump to line ────────────────────────────────────────────────────────

function jumpToLine(line) {
    const lines = editor.value.split('\n');
    let pos = 0;
    for (let i = 0; i < line - 1 && i < lines.length; i++) {
        pos += lines[i].length + 1;
    }
    editor.focus();
    editor.setSelectionRange(pos, pos + (lines[line - 1] || '').length);
}

// ── Share (URL hash) ────────────────────────────────────────────────────

function shareCode() {
    const encoded = btoa(unescape(encodeURIComponent(editor.value)));
    const url = location.origin + location.pathname + '#code=' + encoded;
    history.replaceState(null, '', '#code=' + encoded);

    // Copy to clipboard
    navigator.clipboard.writeText(url).then(() => {
        setStatus('success', 'Link copied to clipboard');
    }).catch(() => {
        setStatus('success', 'URL updated (copy from address bar)');
    });
}

function loadFromHash() {
    const hash = location.hash;
    if (hash.startsWith('#code=')) {
        try {
            const decoded = decodeURIComponent(escape(atob(hash.slice(6))));
            editor.value = decoded;
            updateHighlight();
            return true;
        } catch (e) {
            // Invalid hash, ignore
        }
    }
    return false;
}

// ── Example loading ─────────────────────────────────────────────────────

function loadExample(key) {
    if (EXAMPLES[key]) {
        editor.value = EXAMPLES[key].code;
        updateHighlight();
        output.innerHTML = '<span class="info-line">Click Run or press Ctrl+Enter to execute.</span>';
        lintPanel.innerHTML = '';
        document.getElementById('lint-count').textContent = '';
        setStatus('', '');
        scheduleLint();
    }
}

// ── Divider drag ────────────────────────────────────────────────────────

function initDivider() {
    const divider = document.getElementById('divider');
    const leftPane = document.getElementById('editor-pane');
    const rightPane = document.getElementById('output-pane');
    let isDragging = false;

    divider.addEventListener('mousedown', (e) => {
        isDragging = true;
        divider.classList.add('dragging');
        e.preventDefault();
    });

    document.addEventListener('mousemove', (e) => {
        if (!isDragging) return;
        const container = document.querySelector('.main');
        const rect = container.getBoundingClientRect();
        const pct = ((e.clientX - rect.left) / rect.width) * 100;
        const clamped = Math.max(20, Math.min(80, pct));
        leftPane.style.flex = `0 0 ${clamped}%`;
        rightPane.style.flex = `0 0 ${100 - clamped}%`;
    });

    document.addEventListener('mouseup', () => {
        isDragging = false;
        divider.classList.remove('dragging');
    });
}

// ── Tab key handling ────────────────────────────────────────────────────

function handleTab(e) {
    if (e.key === 'Tab') {
        e.preventDefault();
        const start = editor.selectionStart;
        const end = editor.selectionEnd;
        editor.value = editor.value.substring(0, start) + '    ' + editor.value.substring(end);
        editor.selectionStart = editor.selectionEnd = start + 4;
        updateHighlight();
    }
}

// ── Init ────────────────────────────────────────────────────────────────

function init() {
    // Populate example dropdown
    for (const [key, ex] of Object.entries(EXAMPLES)) {
        const option = document.createElement('option');
        option.value = key;
        option.textContent = ex.name;
        exampleSelect.appendChild(option);
    }

    // Event listeners
    editor.addEventListener('input', () => {
        updateHighlight();
        scheduleLint();
    });
    editor.addEventListener('scroll', syncScroll);
    editor.addEventListener('keydown', handleTab);
    editor.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
            e.preventDefault();
            runCode();
        }
    });

    exampleSelect.addEventListener('change', (e) => {
        if (e.target.value) {
            loadExample(e.target.value);
            e.target.value = '';
        }
    });

    document.getElementById('btn-run').addEventListener('click', runCode);
    document.getElementById('btn-format').addEventListener('click', formatCode);
    document.getElementById('btn-share').addEventListener('click', shareCode);

    initDivider();

    // Load from URL hash or default example
    if (!loadFromHash()) {
        loadExample('hello');
    } else {
        scheduleLint();
    }

    // Init WASM
    setStatus('', 'Loading WASM...');
    initWasm();
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
