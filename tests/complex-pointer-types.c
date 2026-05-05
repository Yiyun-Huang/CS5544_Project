// tests pointers to stack objects & pointers to pointers:


#include <stdlib.h>

int * a; 
int * b;
int ** c;

// int main() {
//     a = malloc(1 * sizeof(int));
//     int x = 10;
//     // a = &x;
//     c = &b;
//     b = &x;

//     a = *c;
//     // *c = a;
//     return 0;
// }

// int main() {
//     a = malloc(1 * sizeof(int));
//     int x = 10;
//     a = &x;
//     c = &b;
//     // b = &x;

//     // a = *c;
//     *c = a;
//     return 0;
// }

// int main() {
//     a = malloc(1 * sizeof(int));
//     int x = 10;
//     c = &b;
//     *c = &x;
//     return 0;
// }

int ** d;

int main() {
    int x = 10;
    c = &b;
    d = &a;
    a = &x;

    *c = *d;
    return 0;
}

