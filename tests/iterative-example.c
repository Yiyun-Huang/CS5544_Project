// Tests the correctness of the iterative worklist algorithm
#include <stdlib.h>

int *a, *b;

int main() {
    a = malloc(sizeof(int));
    b = malloc(sizeof(int));
    *a = 0;
    while(*a < 10) {
        *a = (*a + 1);
        b = malloc(sizeof(int));
    }
    return *a;
}