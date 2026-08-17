/* tsort.h -- header file for topological sort for vlock,
 *            the VT locking program for linux
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

#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include "vlist.h"

/* An edge of the graph, specifying that predecessor must come before
 * successor. */
struct edge
{
  void *predecessor;
  void *successor;
};

static inline struct edge *make_edge(void *p, void *s)
{
  struct edge *e = malloc(sizeof *e);

  if (e == NULL)
    return NULL;

  e->predecessor = p;
  e->successor = s;

  return e;
}

/* For the given directed graph, generate a topological sort of the nodes.
 *
 * Sorts the list and deletes all edges.  If there are circles found in the
 * graph or there are edges that have no corresponding nodes NULL is returned
 * and the erroneous edges are left. */
/* XXX: sort the list in place and return a boolean for success */
VList *tsort(VList *nodes, VList **edges);
