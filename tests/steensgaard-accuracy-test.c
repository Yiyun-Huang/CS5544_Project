// This test demonstrates the decrease in accuracy as a result of Steensgaard's unification


int *p, *q, *r, *s, *t;

int main() {
    int a, b, c, d, e;

    p = &a;
    q = &b;
    r = &c;
    s = &d;
    t = &e;

    p = q;  // create an equiv. class {a, b}
    q = r;  // add {c} to this equiv class. p, q, and r now all point to {a, b, c}
    r = s;  // add {d} to this equiv class. p, q, r, and s now all point to {a, b, c, d}
    s = t;  // add {e} to this equiv class. p, q, r, s, and t now all point to {a, b, c, d, e}
}