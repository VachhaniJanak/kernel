#include "slubutils.h"

#include <mm/slub/slub.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool is_slab_list_empty(kmem_slab_t *slab_ptr) { return slab_ptr == NULL; }

kmem_slab_t *pop_slab(kmem_slab_t **head_ptr) {

  if (head_ptr == NULL || *head_ptr == NULL)
    return NULL;

  kmem_slab_t *ptr = *head_ptr;
  *head_ptr = ptr->nxt;

  if (ptr->nxt != NULL)
    ptr->nxt->prv = NULL;

  ptr->nxt = NULL;
  ptr->prv = NULL;
  return ptr;
}

void push_slab(kmem_slab_t **head_ptr, kmem_slab_t *new_slab) {

  if (head_ptr == NULL || new_slab == NULL)
    return;

  new_slab->nxt = *head_ptr;
  new_slab->prv = NULL;

  if (new_slab->nxt != NULL)
    new_slab->nxt->prv = new_slab;

  *head_ptr = new_slab;
}

void remove_slab(kmem_slab_t **head_ptr, kmem_slab_t *node_ptr) {

  if (head_ptr == NULL || node_ptr == NULL)
    return;

  if (*head_ptr == node_ptr)
    *head_ptr = node_ptr->nxt;

  if (node_ptr->prv != NULL)
    node_ptr->prv->nxt = node_ptr->nxt;

  if (node_ptr->nxt != NULL)
    node_ptr->nxt->prv = node_ptr->prv;

  node_ptr->nxt = NULL;
  node_ptr->prv = NULL;
}
