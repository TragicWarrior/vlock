/* vlist.h -- simple doubly-linked list (libc replacement for GList)
 *
 * Minimal subset of the GList API used by vlock.  Nodes own only the link
 * structure; callers own `data` unless a free function is provided.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef struct VList VList;

struct VList {
  void *data;
  VList *next;
  VList *prev;
};

/* Create a one-element list (malloc; returns NULL on OOM). */
VList *vlist_alloc(void *data);

/* Prepend / append.  Returns the (possibly new) head.  On OOM returns the
 * original list unchanged and leaves errno set. */
VList *vlist_prepend(VList *list, void *data);
VList *vlist_append(VList *list, void *data);

/* Navigation / queries. */
static inline VList *vlist_next(VList *list) { return list ? list->next : NULL; }
static inline VList *vlist_prev(VList *list) { return list ? list->prev : NULL; }
VList *vlist_last(VList *list);
unsigned vlist_length(const VList *list);
int vlist_index(const VList *list, const void *data);

/* Search.  compare(list_data, user_data) like strcmp: 0 means match. */
VList *vlist_find(VList *list, const void *data);
VList *vlist_find_custom(VList *list, const void *data,
                         int (*compare)(const void *a, const void *b));

/* Mutation. */
VList *vlist_remove(VList *list, const void *data);
VList *vlist_delete_link(VList *list, VList *link);
VList *vlist_copy(VList *list);

/* Free the list spine only (not data). */
void vlist_free(VList *list);

/* Free list spine and each data pointer with free(3). */
void vlist_free_full(VList *list, void (*free_func)(void *data));
