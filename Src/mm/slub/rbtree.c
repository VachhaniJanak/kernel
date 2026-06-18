#include "rbtree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern void *pre_obj_alloc(size_t size);
extern void pre_obj_free(void *node);

static inline void *allocate_node(size_t size) { return pre_obj_alloc(size); }
static inline void deallocate_node(void *node) { pre_obj_free(node); }

void slub_rb_create(RBTree *t) {
  t->nil = &t->nil_node;
  t->nil->color = BLACK;
  t->nil->left = t->nil->right = t->nil->parent = t->nil;
  t->root = t->nil;
}

static inline Node *new_node(RBTree *t, void *key, void *value) {
  Node *n = allocate_node(sizeof *n);
  n->key = key;
  n->value = value;

  n->color = RED;
  n->left = n->right = n->parent = t->nil;
  return n;
}

static inline void rotate_left(RBTree *t, Node *x) {
  Node *y = x->right;
  x->right = y->left;

  if (y->left != t->nil)
    y->left->parent = x;

  y->parent = x->parent;

  if (x->parent == t->nil)
    t->root = y;
  else if (x == x->parent->left)
    x->parent->left = y;
  else
    x->parent->right = y;

  y->left = x;
  x->parent = y;
}

static inline void rotate_right(RBTree *t, Node *y) {
  Node *x = y->left;
  y->left = x->right;

  if (x->right != t->nil)
    x->right->parent = y;

  x->parent = y->parent;

  if (y->parent == t->nil)
    t->root = x;
  else if (y == y->parent->right)
    y->parent->right = x;
  else
    y->parent->left = x;

  x->right = y;
  y->parent = x;
}

static inline void insert_fixup(RBTree *t, Node *z) {
  while (z->parent->color == RED) {
    if (z->parent == z->parent->parent->left) {
      Node *y = z->parent->parent->right; /* uncle */

      if (y->color == RED) { /* Case 1 */
        z->parent->color = BLACK;
        y->color = BLACK;
        z->parent->parent->color = RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->right) { /* Case 2 */
          z = z->parent;
          rotate_left(t, z);
        }
        z->parent->color = BLACK; /* Case 3 */
        z->parent->parent->color = RED;
        rotate_right(t, z->parent->parent);
      }
    } else {                             /* mirror */
      Node *y = z->parent->parent->left; /* uncle */

      if (y->color == RED) { /* Case 1 */
        z->parent->color = BLACK;
        y->color = BLACK;
        z->parent->parent->color = RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->left) { /* Case 2 */
          z = z->parent;
          rotate_right(t, z);
        }
        z->parent->color = BLACK; /* Case 3 */
        z->parent->parent->color = RED;
        rotate_left(t, z->parent->parent);
      }
    }
  }
  t->root->color = BLACK;
}

void slub_rb_insert(RBTree *t, void *key, void *value) {
  Node *z = new_node(t, key, value);
  Node *y = t->nil;
  Node *x = t->root;

  while (x != t->nil) {
    y = x;
    if (z->key < x->key)
      x = x->left;
    else
      x = x->right;
  }
  z->parent = y;

  if (y == t->nil)
    t->root = z;
  else if (z->key < y->key)
    y->left = z;
  else
    y->right = z;

  insert_fixup(t, z);
}

/* Replace subtree rooted at u with subtree rooted at v */
static inline void transplant(RBTree *t, Node *u, Node *v) {
  if (u->parent == t->nil)
    t->root = v;
  else if (u == u->parent->left)
    u->parent->left = v;
  else
    u->parent->right = v;
  v->parent = u->parent;
}

static inline Node *tree_minimum(RBTree *t, Node *x) {
  while (x->left != t->nil)
    x = x->left;
  return x;
}

static inline void delete_fixup(RBTree *t, Node *x) {
  while (x != t->root && x->color == BLACK) {
    if (x == x->parent->left) {
      Node *w = x->parent->right;

      if (w->color == RED) { /* Case 1 */
        w->color = BLACK;
        x->parent->color = RED;
        rotate_left(t, x->parent);
        w = x->parent->right;
      }
      if (w->left->color == BLACK && w->right->color == BLACK) {
        w->color = RED; /* Case 2 */
        x = x->parent;
      } else {
        if (w->right->color == BLACK) { /* Case 3 */
          w->left->color = BLACK;
          w->color = RED;
          rotate_right(t, w);
          w = x->parent->right;
        }
        w->color = x->parent->color; /* Case 4 */
        x->parent->color = BLACK;
        w->right->color = BLACK;
        rotate_left(t, x->parent);
        x = t->root;
      }
    } else { /* mirror */
      Node *w = x->parent->left;

      if (w->color == RED) { /* Case 1 */
        w->color = BLACK;
        x->parent->color = RED;
        rotate_right(t, x->parent);
        w = x->parent->left;
      }
      if (w->right->color == BLACK && w->left->color == BLACK) {
        w->color = RED; /* Case 2 */
        x = x->parent;
      } else {
        if (w->left->color == BLACK) { /* Case 3 */
          w->right->color = BLACK;
          w->color = RED;
          rotate_left(t, w);
          w = x->parent->left;
        }
        w->color = x->parent->color; /* Case 4 */
        x->parent->color = BLACK;
        w->left->color = BLACK;
        rotate_right(t, x->parent);
        x = t->root;
      }
    }
  }
  x->color = BLACK;
}

bool slub_rb_delete(RBTree *t, void *key) {
  /* Find the node */
  Node *z = t->root;
  while (z != t->nil)
    if (key < z->key)
      z = z->left;
    else if (key > z->key)
      z = z->right;
    else
      break;
  if (z == t->nil)
    return false;

  Node *y = z;
  Color y_orig_color = y->color;
  Node *x;

  if (z->left == t->nil) {
    x = z->right;
    transplant(t, z, z->right);
  } else if (z->right == t->nil) {
    x = z->left;
    transplant(t, z, z->left);
  } else {
    y = tree_minimum(t, z->right);
    y_orig_color = y->color;
    x = y->right;

    if (y->parent == z) {
      x->parent = y;
    } else {
      transplant(t, y, y->right);
      y->right = z->right;
      y->right->parent = y;
    }
    transplant(t, z, y);
    y->left = z->left;
    y->left->parent = y;
    y->color = z->color;
  }

  deallocate_node(z);

  if (y_orig_color == BLACK)
    delete_fixup(t, x);

  return true;
}

void *slub_rb_search(RBTree *t, void *key) {
  Node *x = t->root;
  while (x != t->nil)
    if (key < x->key)
      x = x->left;
    else if (key > x->key)
      x = x->right;
    else
      return x->value;
  return NULL;
}
