// =============================================================================
// linked-list.c -- macro-benchmark for flow-sensitive points-to analysis
//
// Exercises pointer-heavy patterns at a scale ~5-10x our micro suite:
//   - dynamic allocation via malloc returning struct pointers
//   - pointer-to-pointer manipulations (head/tail bookkeeping)
//   - conditional reassignment of node pointers in branches and loops
//   - struct field stores/loads through GEP-lowered pointer offsets
//   - reverse-in-place: classic three-pointer dance that stresses
//     strong-vs-weak update tracking
//
// The functions are written so that a flow-INsensitive analysis would
// conservatively conclude that almost every pointer may alias, while a
// flow-SENSITIVE analysis can recover precision at intermediate points.
// =============================================================================

#include <stdlib.h>

typedef struct Node {
    int val;
    struct Node* next;
} Node;

static Node* g_head;
static Node* g_tail;

// ---- creation ---------------------------------------------------------------
Node* make_node(int v) {
    Node* n = (Node*) malloc(sizeof(Node));
    n->val = v;
    n->next = (Node*) 0;
    return n;
}

// ---- head insertion (strong update on g_head every call) --------------------
void push_front(int v) {
    Node* n = make_node(v);
    n->next = g_head;
    g_head = n;
    if (g_tail == (Node*) 0) {
        g_tail = n;
    }
}

// ---- tail insertion (mixed: strong on g_tail when list empty, weak when
// inside the if-branch updating an existing tail->next) ----------------------
void push_back(int v) {
    Node* n = make_node(v);
    if (g_head == (Node*) 0) {
        g_head = n;
        g_tail = n;
    } else {
        g_tail->next = n;
        g_tail = n;
    }
}

// ---- pop front: returns the popped node, advances head ----------------------
Node* pop_front(void) {
    if (g_head == (Node*) 0) {
        return (Node*) 0;
    }
    Node* old = g_head;
    g_head = g_head->next;
    if (g_head == (Node*) 0) {
        g_tail = (Node*) 0;
    }
    old->next = (Node*) 0;
    return old;
}

// ---- linear search; returns pointer to node, or NULL ------------------------
Node* find(int v) {
    Node* cur = g_head;
    while (cur != (Node*) 0) {
        if (cur->val == v) {
            return cur;
        }
        cur = cur->next;
    }
    return (Node*) 0;
}

// ---- in-place reverse: prev / cur / next dance.
// This is the classic test case for a points-to analysis: each of the three
// locals takes on different points-to sets at different program points within
// the same loop iteration.
void reverse(void) {
    Node* prev = (Node*) 0;
    Node* cur = g_head;
    Node* next;
    while (cur != (Node*) 0) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    g_tail = g_head;
    g_head = prev;
}

// ---- splice another list onto our tail --------------------------------------
void append_list(Node* other_head, Node* other_tail) {
    if (other_head == (Node*) 0) {
        return;
    }
    if (g_head == (Node*) 0) {
        g_head = other_head;
        g_tail = other_tail;
    } else {
        g_tail->next = other_head;
        g_tail = other_tail;
    }
}

// ---- driver: builds, mutates, and traverses the list -----------------------
int main(void) {
    g_head = (Node*) 0;
    g_tail = (Node*) 0;

    push_back(10);
    push_back(20);
    push_back(30);
    push_front(5);

    Node* found = find(20);
    int found_val = 0;
    if (found != (Node*) 0) {
        found_val = found->val;
    }

    Node* extra_head = make_node(100);
    Node* extra_mid  = make_node(200);
    Node* extra_tail = make_node(300);
    extra_head->next = extra_mid;
    extra_mid->next  = extra_tail;
    append_list(extra_head, extra_tail);

    reverse();

    int sum = 0;
    Node* cur = g_head;
    while (cur != (Node*) 0) {
        sum = sum + cur->val;
        cur = cur->next;
    }

    Node* popped = pop_front();
    int popped_val = 0;
    if (popped != (Node*) 0) {
        popped_val = popped->val;
        free(popped);
    }

    return sum + found_val + popped_val;
}
