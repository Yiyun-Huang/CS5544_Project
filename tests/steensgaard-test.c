// A pathological example that represents the worst case in the Andersen algorithm
// This example demonstrates the speedup offered by the Steensgaard algorithm

#include <stdlib.h>

int *a, *b, *c;

int **p, **q;

int main() {
    int x = 10;
    int x1 = 10;
    int x2 = 10;
    int x3 = 10;
    int x4 = 10;
    int x5 = 10;
    int x6 = 10;
    int x7 = 10;
    int x8 = 10;
    int x9 = 10;
    int x10 = 10;
    int x11 = 10;
    int x12 = 10;
    int x13 = 10;
    int x14 = 10;

    int y = 10;
    int y1 = 10;
    int y2 = 10;
    int y3 = 10;
    int y4 = 10;
    int y5 = 10;
    int y6 = 10;
    int y7 = 10;
    int y8 = 10;
    int y9 = 10;
    int y10 = 10;
    int y11 = 10;
    int y12 = 10;
    int y13 = 10;
    int y14 = 10;


    a = &x;
    a = &x1;
    a = &x2;
    a = &x3;
    a = &x4;

    b = &x5;
    b = &x6;
    b = &x7;
    b = &x8;
    b = &x9;

    c = &x10;
    c = &x11;
    c = &x12;
    c = &x13;
    c = &x14;

    a = &y;
    a = &y1;
    a = &y2;
    a = &y3;
    a = &y4;

    b = &y5;
    b = &y6;
    b = &y7;
    b = &y8;
    b = &y9;

    c = &y10;
    c = &y11;
    c = &y12;
    c = &y13;
    c = &y14;

    int w = 10;
    int w1 = 10;
    int w2 = 10;
    int w3 = 10;
    int w4 = 10;
    int w5 = 10;
    int w6 = 10;
    int w7 = 10;
    int w8 = 10;
    int w9 = 10;
    int w10 = 10;
    int w11 = 10;
    int w12 = 10;
    int w13 = 10;
    int w14 = 10;

    int z = 10;
    int z1 = 10;
    int z2 = 10;
    int z3 = 10;
    int z4 = 10;
    int z5 = 10;
    int z6 = 10;
    int z7 = 10;
    int z8 = 10;
    int z9 = 10;
    int z10 = 10;
    int z11 = 10;
    int z12 = 10;
    int z13 = 10;
    int z14 = 10;

    a = &w;
    a = &w1;
    a = &w2;
    a = &w3;
    a = &w4;

    b = &w5;
    b = &w6;
    b = &w7;
    b = &w8;
    b = &w9;

    c = &w10;
    c = &w11;
    c = &w12;
    c = &w13;
    c = &w14;

    a = &z;
    a = &z1;
    a = &z2;
    a = &z3;
    a = &z4;

    b = &z5;
    b = &z6;
    b = &z7;
    b = &z8;
    b = &z9;

    c = &z10;
    c = &z11;
    c = &z12;
    c = &z13;
    c = &z14;

    int f = 10;
    int f1 = 10;
    int f2 = 10;
    int f3 = 10;
    int f4 = 10;
    int f5 = 10;
    int f6 = 10;
    int f7 = 10;
    int f8 = 10;
    int f9 = 10;
    int f10 = 10;
    int f11 = 10;
    int f12 = 10;
    int f13 = 10;
    int f14 = 10;

    int k = 10;
    int k1 = 10;
    int k2 = 10;
    int k3 = 10;
    int k4 = 10;
    int k5 = 10;
    int k6 = 10;
    int k7 = 10;
    int k8 = 10;
    int k9 = 10;
    int k10 = 10;
    int k11 = 10;
    int k12 = 10;
    int k13 = 10;
    int k14 = 10;

    a = &f;
    a = &f1;
    a = &f2;
    a = &f3;
    a = &f4;

    b = &f5;
    b = &f6;
    b = &f7;
    b = &f8;
    b = &f9;

    c = &f10;
    c = &f11;
    c = &f12;
    c = &f13;
    c = &f14;

    a = &k;
    a = &k1;
    a = &k2;
    a = &k3;
    a = &k4;

    b = &k5;
    b = &k6;
    b = &k7;
    b = &k8;
    b = &k9;

    c = &k10;
    c = &k11;
    c = &k12;
    c = &k13;
    c = &k14;
    

    p = &a;
    p = &b;
    p = &c;

    q = *p;
}
