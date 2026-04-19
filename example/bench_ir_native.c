// bench_ir_native.c — Exact C equivalent of bench_ir.hexa
// For LLVM comparison: clang -O3
#include <stdio.h>
#include <stdint.h>

static int64_t count_primes(int64_t limit) {
    int64_t count = 0;
    int64_t n = 2;
    while (n < limit) {
        int64_t is_prime = 1;
        int64_t d = 2;
        while (d * d <= n) {
            if (n % d == 0) {
                is_prime = 0;
            }
            d = d + 1;
        }
        count = count + is_prime;
        n = n + 1;
    }
    return count;
}

static int64_t fib_iter(int64_t n) {
    int64_t a = 0;
    int64_t b = 1;
    int64_t i = 0;
    while (i < n) {
        int64_t t = a + b;
        a = b;
        b = t;
        i = i + 1;
    }
    return a;
}

static int64_t gcd(int64_t a, int64_t b) {
    int64_t x = a;
    int64_t y = b;
    while (y != 0) {
        int64_t t = y;
        y = x % y;
        x = t;
    }
    return x;
}

static int64_t collatz_steps(int64_t n) {
    int64_t steps = 0;
    int64_t v = n;
    while (v != 1) {
        if (v % 2 == 0) {
            v = v / 2;
        } else {
            v = v * 3 + 1;
        }
        steps = steps + 1;
    }
    return steps;
}

static int64_t isqrt(int64_t n) {
    int64_t result = 0;
    if (n < 2) {
        result = n;
    } else {
        int64_t x = n;
        int64_t y = (x + 1) / 2;
        while (y < x) {
            x = y;
            y = (x + n / x) / 2;
        }
        result = x;
    }
    return result;
}

int main(void) {
    int64_t iterations = 10000;
    int64_t result = 0;

    // [1] count_primes(100) = 25
    int64_t i = 0;
    while (i < iterations) {
        result = result + count_primes(100);
        i = i + 1;
    }

    // [2] fib(30) = 832040
    i = 0;
    while (i < iterations) {
        result = result + fib_iter(30);
        i = i + 1;
    }

    // [3] gcd(48,18) = 6
    i = 0;
    while (i < iterations * 10) {
        result = result + gcd(48, 18);
        i = i + 1;
    }

    // [4] collatz(27) = 111
    i = 0;
    while (i < iterations) {
        result = result + collatz_steps(27);
        i = i + 1;
    }

    // [5] isqrt(1000000) = 1000
    i = 0;
    while (i < iterations * 10) {
        result = result + isqrt(1000000);
        i = i + 1;
    }

    printf("%lld\n", result);
    return 0;
}
