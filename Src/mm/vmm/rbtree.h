#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { RED, BLACK } Color;

typedef struct Node {
  // data
  void *addr;
  size_t size;
  bool is_continuous;

  Color color;
  struct Node *left, *right, *parent;
} Node;

typedef struct {
  Node *root;
  Node *nil;     /* sentinel: shared "NULL" leaf, always BLACK */
  Node nil_node; /* sentinel: shared "NULL" leaf, always BLACK */
} RBTree;

void rb_create(RBTree *t);
void rb_insert(RBTree *t, void *addr, size_t size, bool is_continuous);
bool rb_delete(RBTree *t, void *addr, size_t *size, bool *is_continuous);
Node *rb_search(RBTree *t, void *addr);
