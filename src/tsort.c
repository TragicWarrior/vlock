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
#include <errno.h>

#include "util.h"

#include "tsort.h"

/* Get the zeros of the graph, i.e. nodes with no incoming edges. */
static GList *get_zeros(GList *nodes, GList *edges)
{
  GList *zeros = g_list_copy(nodes);

  for (GList *edge_item = edges;
       edge_item != NULL;
       edge_item = g_list_next(edge_item)) {
    struct edge *e = edge_item->data;
    zeros = g_list_remove(zeros, e->successor);
  }

  return zeros;
}

/* Check if the given node is a zero. */
static bool is_zero(void *node, GList *edges)
{
  for (GList *edge_item = edges;
       edge_item != NULL;
       edge_item = g_list_next(edge_item)) {
    struct edge *e = edge_item->data;

    if (e->successor == node)
      return false;
  }

  return true;
}

/* For the given directed graph, generate a topological sort of the nodes.
 *
 * Sorts the list and deletes all edges.  If there are circles found in the
 * graph or there are edges that have no corresponding nodes the erroneous
 * edges are left.
 *
 * The algorithm is taken from the Wikipedia:
 *
 * http://en.wikipedia.org/w/index.php?title=Topological_sorting&oldid=153157450#Algorithms
 *
 */
GList *tsort(GList *nodes, GList **edges)
{
  /* Retrieve all zeros. */
  GList *zeros = get_zeros(nodes, *edges);

  GList *sorted_nodes = NULL;

  /* While the list of zeros is not empty ... */
  while (zeros != NULL) {
    /* ... take the first zero and remove it and ...*/
    void *zero = zeros->data;
    zeros = g_list_delete_link(zeros, zeros);

    /* ... add it to the list of sorted nodes. */
    sorted_nodes = g_list_append(sorted_nodes, zero);

    /* Then look at each edge ... */
    for (GList *edge_item = *edges;
         edge_item != NULL;) {
      struct edge *e = edge_item->data;

      GList *tmp = g_list_next(edge_item);

      /* ... that has this zero as its predecessor ... */
      if (e->predecessor == zero) {
        /* ... and remove it. */
        *edges = g_list_delete_link(*edges, edge_item);

        /* If the successor has become a zero now ... */
        if (is_zero(e->successor, *edges))
          /* ... add it to the list of zeros. */
          zeros = g_list_append(zeros, e->successor);

        g_free(e);
      }

      edge_item = tmp;
    }
  }

  /* If all edges were deleted the algorithm was successful. */
  if (*edges != NULL) {
    g_list_free(sorted_nodes);
    sorted_nodes = NULL;
  }

  g_list_free(zeros);

  return sorted_nodes;
  ;
}

