// Example from lecture 14 slides

#include <stdlib.h>

int *a, *b;


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

