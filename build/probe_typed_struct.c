#include <stdio.h>


int main(void) {
    long t[2] = {"Int", "42"};
    printf("%ld %ld\n", (long)(t[0]), (long)(t[1]));
    return 0;
}
