#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { RED, BLACK } Color;

typedef struct Node {
  // data
  void *key;
  void *value;

  Color color;
  struct Node *left, *right, *parent;
} Node;

typedef struct {
  Node *root;
  Node *nil;     /* sentinel: shared "NULL" leaf, always BLACK */
  Node nil_node; /* sentinel: shared "NULL" leaf, always BLACK */
} RBTree;

void slub_rb_create(RBTree *t);
void slub_rb_insert(RBTree *t, void *key, void *value);
bool slub_rb_delete(RBTree *t, void *key);
void *slub_rb_search(RBTree *t, void *key);
