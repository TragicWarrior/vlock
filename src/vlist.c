/* vlist.c -- simple doubly-linked list (libc replacement for GList) */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "vlist.h"

VList *vlist_alloc(void *data)
{
  VList *n = malloc(sizeof *n);

  if (n == NULL)
    return NULL;

  n->data = data;
  n->next = NULL;
  n->prev = NULL;
  return n;
}

VList *vlist_prepend(VList *list, void *data)
{
  VList *n = vlist_alloc(data);

  if (n == NULL)
    return list;

  n->next = list;
  if (list != NULL)
    list->prev = n;
  return n;
}

VList *vlist_append(VList *list, void *data)
{
  VList *n = vlist_alloc(data);

  if (n == NULL)
    return list;

  if (list == NULL)
    return n;

  VList *last = vlist_last(list);
  last->next = n;
  n->prev = last;
  return list;
}

VList *vlist_last(VList *list)
{
  if (list == NULL)
    return NULL;

  while (list->next != NULL)
    list = list->next;
  return list;
}

unsigned vlist_length(const VList *list)
{
  unsigned n = 0;

  for (; list != NULL; list = list->next)
    n++;
  return n;
}

int vlist_index(const VList *list, const void *data)
{
  int i = 0;

  for (; list != NULL; list = list->next, i++)
    if (list->data == data)
      return i;
  return -1;
}

VList *vlist_find(VList *list, const void *data)
{
  for (; list != NULL; list = list->next)
    if (list->data == data)
      return list;
  return NULL;
}

VList *vlist_find_custom(VList *list, const void *data,
                         int (*compare)(const void *a, const void *b))
{
  for (; list != NULL; list = list->next)
    if (compare(list->data, data) == 0)
      return list;
  return NULL;
}

VList *vlist_remove(VList *list, const void *data)
{
  VList *link = vlist_find(list, data);

  if (link == NULL)
    return list;
  return vlist_delete_link(list, link);
}

VList *vlist_delete_link(VList *list, VList *link)
{
  if (link == NULL)
    return list;

  if (link->prev != NULL)
    link->prev->next = link->next;
  else
    list = link->next;

  if (link->next != NULL)
    link->next->prev = link->prev;

  free(link);
  return list;
}

VList *vlist_copy(VList *list)
{
  VList *copy = NULL;
  VList *tail = NULL;

  for (; list != NULL; list = list->next) {
    VList *n = vlist_alloc(list->data);

    if (n == NULL) {
      /* Best-effort: return partial copy.  Callers treat this like OOM. */
      return copy;
    }

    if (copy == NULL)
      copy = n;
    else {
      tail->next = n;
      n->prev = tail;
    }
    tail = n;
  }
  return copy;
}

void vlist_free(VList *list)
{
  while (list != NULL)
    list = vlist_delete_link(list, list);
}

void vlist_free_full(VList *list, void (*free_func)(void *data))
{
  while (list != NULL) {
    if (free_func != NULL && list->data != NULL)
      free_func(list->data);
    list = vlist_delete_link(list, list);
  }
}
