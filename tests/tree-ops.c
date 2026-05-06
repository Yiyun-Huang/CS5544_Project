// =============================================================================
// tree-ops.c -- macro-benchmark for flow-sensitive points-to analysis
//
// Binary-search-tree operations: insert, search, count, mirror, sum, and
// destructive cleanup. Multi-level pointer indirection (each Tree node holds
// two child pointers and a parent pointer) stresses both load/store transfer
// functions and the merge step at branch joins.
//
// Compared to the linked-list benchmark, this program has:
//   - more abstract objects (every malloc site contributes one)
//   - deeper conditional control flow (if/else inside while, recursion-like
//     iterative patterns)
//   - pointer reads that update through pointer-to-pointer chains
//     (`*cursor = node` style traversal)
// =============================================================================

#include <stdlib.h>

typedef struct Tree {
    int key;
    struct Tree* left;
    struct Tree* right;
    struct Tree* parent;
} Tree;

static Tree* g_root;
static int g_count;

// ---- node allocation --------------------------------------------------------
Tree* make_tree_node(int k) {
    Tree* t = (Tree*) malloc(sizeof(Tree));
    t->key    = k;
    t->left   = (Tree*) 0;
    t->right  = (Tree*) 0;
    t->parent = (Tree*) 0;
    return t;
}

// ---- iterative insert: walks down, links new node under correct parent.
// The cursor pointer takes on a different points-to set on each iteration of
// the while loop, which is exactly the kind of intermediate-state precision
// flow-sensitive analysis is supposed to recover.
void tree_insert(int k) {
    Tree* node = make_tree_node(k);
    if (g_root == (Tree*) 0) {
        g_root = node;
        return;
    }
    Tree* cur = g_root;
    Tree* parent = (Tree*) 0;
    while (cur != (Tree*) 0) {
        parent = cur;
        if (k < cur->key) {
            cur = cur->left;
        } else {
            cur = cur->right;
        }
    }
    node->parent = parent;
    if (k < parent->key) {
        parent->left = node;
    } else {
        parent->right = node;
    }
}

// ---- search: returns pointer to matching node or NULL -----------------------
Tree* tree_find(int k) {
    Tree* cur = g_root;
    while (cur != (Tree*) 0) {
        if (k == cur->key) {
            return cur;
        }
        if (k < cur->key) {
            cur = cur->left;
        } else {
            cur = cur->right;
        }
    }
    return (Tree*) 0;
}

// ---- iterative size count using an explicit stack of pointers ---------------
int tree_size(void) {
    if (g_root == (Tree*) 0) {
        return 0;
    }
    Tree* stack[64];
    int top = 0;
    stack[0] = g_root;
    top = 1;

    int count = 0;
    while (top > 0) {
        top = top - 1;
        Tree* cur = stack[top];
        count = count + 1;
        if (cur->left != (Tree*) 0) {
            stack[top] = cur->left;
            top = top + 1;
        }
        if (cur->right != (Tree*) 0) {
            stack[top] = cur->right;
            top = top + 1;
        }
    }
    return count;
}

// ---- mirror: swap left/right of every node, iteratively --------------------
void tree_mirror(void) {
    if (g_root == (Tree*) 0) {
        return;
    }
    Tree* stack[64];
    int top = 0;
    stack[0] = g_root;
    top = 1;

    while (top > 0) {
        top = top - 1;
        Tree* cur = stack[top];
        Tree* tmp = cur->left;
        cur->left = cur->right;
        cur->right = tmp;
        if (cur->left != (Tree*) 0) {
            stack[top] = cur->left;
            top = top + 1;
        }
        if (cur->right != (Tree*) 0) {
            stack[top] = cur->right;
            top = top + 1;
        }
    }
}

// ---- iterative sum of all keys ----------------------------------------------
int tree_sum_keys(void) {
    if (g_root == (Tree*) 0) {
        return 0;
    }
    Tree* stack[64];
    int top = 0;
    stack[0] = g_root;
    top = 1;

    int total = 0;
    while (top > 0) {
        top = top - 1;
        Tree* cur = stack[top];
        total = total + cur->key;
        if (cur->left != (Tree*) 0) {
            stack[top] = cur->left;
            top = top + 1;
        }
        if (cur->right != (Tree*) 0) {
            stack[top] = cur->right;
            top = top + 1;
        }
    }
    return total;
}

// ---- destroy the tree, freeing every node ----------------------------------
void tree_destroy(void) {
    if (g_root == (Tree*) 0) {
        return;
    }
    Tree* stack[64];
    int top = 0;
    stack[0] = g_root;
    top = 1;

    while (top > 0) {
        top = top - 1;
        Tree* cur = stack[top];
        if (cur->left != (Tree*) 0) {
            stack[top] = cur->left;
            top = top + 1;
        }
        if (cur->right != (Tree*) 0) {
            stack[top] = cur->right;
            top = top + 1;
        }
        free(cur);
    }
    g_root = (Tree*) 0;
}

// ---- driver -----------------------------------------------------------------
int main(void) {
    g_root = (Tree*) 0;
    g_count = 0;

    tree_insert(50);
    tree_insert(30);
    tree_insert(70);
    tree_insert(20);
    tree_insert(40);
    tree_insert(60);
    tree_insert(80);
    tree_insert(10);
    tree_insert(35);

    Tree* found = tree_find(40);
    int found_key = 0;
    if (found != (Tree*) 0) {
        found_key = found->key;
    }

    int sz = tree_size();
    int total = tree_sum_keys();

    tree_mirror();

    Tree* found_after = tree_find(40);
    int after_key = 0;
    if (found_after != (Tree*) 0) {
        after_key = found_after->key;
    }

    tree_destroy();

    return sz + total + found_key + after_key;
}
