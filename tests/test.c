// int foo(int * ptr) {
//     return *ptr;
// }

// int main() {
//     int x, y, *p;
//     p = &x;
//     foo(p);
//     p = &y;
//     return *p;
// }

#include <stdlib.h>

int *a, *b;

// void f() {
//     q = malloc(sizeof(int) * 5);
//     r = malloc(sizeof(int) * 5);
// }

int main() {
    a = malloc(sizeof(int) * 5);
    b = malloc(sizeof(int) * 5);
    int c = 1;
    a = b;
    a = malloc(sizeof(int) * 6);
    if (c) {
        a = b;
    } else {
        a = malloc(sizeof(int) * 4);
    }

    int x = *a;
    return x;
}