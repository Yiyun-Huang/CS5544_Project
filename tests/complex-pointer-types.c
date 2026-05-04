// tests pointers to stack objects & pointers to pointers:


#include <stdlib.h>

int * a; 
int * b;
int ** c;

int main() {
    a = malloc(1 * sizeof(int));
    int x = 10;
    a = &x;
    c = &b;
    b = &x;
    return 0;
}

