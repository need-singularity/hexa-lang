// HEXA-IR Benchmark Suite — C ceiling (동일 로직)
#include <stdio.h>
#include <stdlib.h>

long count_primes(long limit) {
    long count = 0;
    for (long n = 2; n < limit; n++) {
        int is_prime = 1;
        for (long d = 2; d * d <= n; d++) {
            if (n % d == 0) { is_prime = 0; break; }
        }
        count += is_prime;
    }
    return count;
}

long fib_iter(long n) {
    long a = 0, b = 1;
    for (long i = 0; i < n; i++) { long t = a + b; a = b; b = t; }
    return a;
}

long gcd(long a, long b) {
    while (b != 0) { long t = b; b = a % b; a = t; }
    return a;
}

long collatz_steps(long n) {
    long steps = 0, v = n;
    while (v != 1) {
        if (v % 2 == 0) v /= 2; else v = v * 3 + 1;
        steps++;
    }
    return steps;
}

long isqrt(long n) {
    if (n < 2) return n;
    long x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

long hash_range(long start, long end) {
    long hash = 2166136261L;
    for (long i = start; i < end; i++) {
        hash = hash * 16777619L;
        hash = hash + i;
    }
    if (hash < 0) hash = -hash;
    return hash % 1000000007L;
}

int main() {
    long iterations = 10000;
    long result = 0;

    for (long i = 0; i < iterations; i++) result += count_primes(100);
    for (long i = 0; i < iterations; i++) result += fib_iter(30);
    for (long i = 0; i < iterations * 10; i++) result += gcd(48, 18);
    for (long i = 0; i < iterations; i++) result += collatz_steps(27);
    for (long i = 0; i < iterations * 10; i++) result += isqrt(1000000);
    for (long i = 0; i < iterations; i++) result += hash_range(0, 100);

    printf("%ld\n", result);
    return 0;
}
