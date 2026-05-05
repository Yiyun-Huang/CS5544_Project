// tests pointers to stack objects & pointers to pointers:


#include <stdlib.h>

int * a; 
int * b;
int * e;
int * f;
int * g;
int ** c;
int ** d;

int main() {
    int x = 10;
    int y = 20;
    a = &x;
    c = &b;
    *c = a;
    
    e = *c;

    d = &f;
    *d = &y;

    if (x) {
        *d = *c;
    }
    g = f;
}
