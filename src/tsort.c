/* tsort.c -- topological sort for vlock, the VT locking program for linux
 *
 * This program is copyright (C) 2007 Frank Benkstein, and is free
 * software which is freely distributable under the terms of the
 * GNU General Public License version 2, included as the file COPYING in this
 * distribution.  It is NOT public domain software, and any
 * redistribution not permitted by the GNU General Public License is
 * expressly forbidden without prior written permission from
 * the author.
 *
 */

#include <stdlib.h>
#include "tsort.h"

/* Get all nodes with no incoming edges. */
static VList *get_zeros(VList *nodes, VList *edges)
{
  VList *zeros = vlist_copy(nodes);

  for (VList *edge_item = edges;
       edge_item != NULL;
       edge_item = vlist_next(edge_item)) {
    struct edge *e = edge_item->data;
    zeros = vlist_remove(zeros, e->successor);
  }

  return zeros;
}

static bool is_zero(void *node, VList *edges)
{
  for (VList *edge_item = edges;
       edge_item != NULL;
       edge_item = vlist_next(edge_item)) {
    struct edge *e = edge_item->data;

    if (e->successor == node)
      return false;
  }

  return true;
}

/* For the given directed graph, generate a topological sort of the nodes.
 *
 * Sorts the list and deletes all edges.  If there are circles found in the
 * graph or there are edges that have no corresponding nodes NULL is returned
 * and the erroneous edges are left. */
VList *tsort(VList *nodes, VList **edges)
{
  /* The algorithm is simple here: Keep finding nodes that are not depending on
   * any other node, put them into the sorted list and remove the edges that
   * this node is part of.  When there are no such "zero" nodes left we are
   * either done or there is an error and there is a cycle in the graph. */

  VList *zeros = get_zeros(nodes, *edges);
  /* Sorted list of nodes. */
  VList *sorted_nodes = NULL;

  while (zeros != NULL) {
    void *zero = zeros->data;
    zeros = vlist_delete_link(zeros, zeros);

    /* Append the zero to the list of sorted nodes. */
    sorted_nodes = vlist_append(sorted_nodes, zero);

    /* Remove all edges that have this zero as a predecessor. */
    for (VList *edge_item = *edges;
         edge_item != NULL; ) {
      struct edge *e = edge_item->data;
      VList *tmp = vlist_next(edge_item);

      if (e->predecessor == zero) {
        void *successor = e->successor;

        *edges = vlist_delete_link(*edges, edge_item);
        free(e);

        /* If the successor has become a zero now append it to the list of
         * zeros. */
        if (is_zero(successor, *edges))
          zeros = vlist_append(zeros, successor);
      }

      edge_item = tmp;
    }
  }

  if (*edges != NULL) {
    /* There are still edges left: there was a cycle.  Clean up and return
     * NULL. */
    vlist_free(sorted_nodes);
    return NULL;
  }

  vlist_free(zeros);
  return sorted_nodes;
}
