// example from Assignment 3 part 2

#include <stdlib.h>


int* p;
int* q;
int* r;

int main()  {
    int a,b,c = 0;
    p = malloc(1* sizeof(int));
    q = malloc(1* sizeof(int));

    p = q;

    r = malloc(1* sizeof(int));

    q = r;

    if (a) {
        p = r;
    } else {
        p = malloc(2 * sizeof(int));
    }

    if (b) p = q;
    int s = *p;
    if (c) r = p;
    int x = *r;
    return x + s;
}
