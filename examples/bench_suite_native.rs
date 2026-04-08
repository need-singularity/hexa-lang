// HEXA-IR Benchmark Suite — Rust ceiling (동일 로직)

fn count_primes(limit: i64) -> i64 {
    let mut count = 0i64;
    for n in 2..limit {
        let mut is_prime = true;
        let mut d = 2;
        while d * d <= n { if n % d == 0 { is_prime = false; break; } d += 1; }
        if is_prime { count += 1; }
    }
    count
}

fn fib_iter(n: i64) -> i64 {
    let (mut a, mut b) = (0i64, 1i64);
    for _ in 0..n { let t = a + b; a = b; b = t; }
    a
}

fn gcd(mut a: i64, mut b: i64) -> i64 {
    while b != 0 { let t = b; b = a % b; a = t; }
    a
}

fn collatz_steps(n: i64) -> i64 {
    let (mut steps, mut v) = (0i64, n);
    while v != 1 { if v % 2 == 0 { v /= 2; } else { v = v * 3 + 1; } steps += 1; }
    steps
}

fn isqrt(n: i64) -> i64 {
    if n < 2 { return n; }
    let (mut x, mut y) = (n, (n + 1) / 2);
    while y < x { x = y; y = (x + n / x) / 2; }
    x
}

fn hash_range(start: i64, end: i64) -> i64 {
    let mut hash = 2166136261i64;
    for i in start..end { hash = hash.wrapping_mul(16777619); hash = hash.wrapping_add(i); }
    if hash < 0 { hash = -hash; }
    hash % 1000000007
}

fn main() {
    let iterations = 10000i64;
    let mut result = 0i64;
    for _ in 0..iterations { result += count_primes(100); }
    for _ in 0..iterations { result += fib_iter(30); }
    for _ in 0..iterations*10 { result += gcd(48, 18); }
    for _ in 0..iterations { result += collatz_steps(27); }
    for _ in 0..iterations*10 { result += isqrt(1000000); }
    for _ in 0..iterations { result += hash_range(0, 100); }
    println!("{}", result);
}
