// Native C benchmark — same logic as bench_loop.hexa
// This represents the theoretical ceiling (bare metal speed)
#include <stdio.h>

long sigma(long n) {
    long sum = 0;
    for (long i = 1; i <= n; i++) {
        if (n % i == 0) sum += i;
    }
    return sum;
}

long phi(long n) {
    long count = 0;
    for (long i = 1; i < n; i++) {
        long a = i, b = n;
        while (b != 0) { long t = b; b = a % b; a = t; }
        if (a == 1) count++;
    }
    return count;
}

long fib(long n) {
    long a = 0, b = 1;
    for (long i = 0; i < n; i++) { long t = a + b; a = b; b = t; }
    return a;
}

int main() {
    long iterations = 100000;
    long total = 0;
    for (long i = 0; i < iterations; i++) {
        total += sigma(6);
        total += phi(6);
        total += fib(20);
    }
    printf("%ld\n", total);
    return 0;
}
