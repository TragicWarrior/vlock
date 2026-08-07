#include <stdlib.h>

#include <CUnit/CUnit.h>

#include "tsort.h"
#include "vlist.h"

#include "test_tsort.h"

#define A ((void *)1)
#define B ((void *)2)
#define C ((void *)3)
#define D ((void *)4)
#define E ((void *)5)
#define F ((void *)6)
#define G ((void *)7)
#define H ((void *)8)

static VList *get_test_list(void)
{
  VList *list = NULL;

  list = vlist_prepend(list, A);
  list = vlist_prepend(list, B);
  list = vlist_prepend(list, C);
  list = vlist_prepend(list, D);
  list = vlist_prepend(list, E);
  list = vlist_prepend(list, F);
  list = vlist_prepend(list, G);
  list = vlist_prepend(list, H);

  return list;
}

static VList *get_test_edges(void)
{
  VList *edges = NULL;

  /* Edges:
   *
   *  E
   *  |
   *  B C D   H
   *   \|/    |
   *    A   F G
   */
  edges = vlist_append(edges, make_edge(A, B));
  edges = vlist_append(edges, make_edge(A, C));
  edges = vlist_append(edges, make_edge(A, D));
  edges = vlist_append(edges, make_edge(B, E));
  edges = vlist_append(edges, make_edge(G, H));

  return edges;
}

static VList *get_faulty_test_edges(void)
{
  VList *edges = NULL;

  /* Edges:
   *
   *  F
   *  |
   *  E
   *  |
   *  B C D   H
   *   \|/    |
   *    A     G
   *    |
   *    F
   *
   */

  edges = vlist_append(edges, make_edge(A, B));
  edges = vlist_append(edges, make_edge(A, C));
  edges = vlist_append(edges, make_edge(A, D));
  edges = vlist_append(edges, make_edge(B, E));
  edges = vlist_append(edges, make_edge(E, F));
  edges = vlist_append(edges, make_edge(F, A));
  edges = vlist_append(edges, make_edge(G, H));

  return edges;
}

void test_tsort_succeed(void)
{
  VList *list = get_test_list();
  VList *edges = get_test_edges();
  VList *sorted_list = tsort(list, &edges);

  CU_ASSERT_PTR_NULL(edges);

  CU_ASSERT_PTR_NOT_NULL(sorted_list);

  CU_ASSERT_EQUAL(vlist_length(list), vlist_length(sorted_list));

  /* Check that all items from the original list are in the sorted list. */
  for (VList *item = list; item != NULL; item = vlist_next(item))
    CU_ASSERT_PTR_NOT_NULL(vlist_find(sorted_list, item->data));

  /* Check that all items are in the order that is given by the edges. */
  edges = get_test_edges();

  while (edges != NULL) {
    struct edge *e = edges->data;
    CU_ASSERT(vlist_index(sorted_list, e->predecessor) < vlist_index(sorted_list, e->successor));
    free(e);
    edges = vlist_delete_link(edges, edges);
  }

  vlist_free(sorted_list);
  vlist_free(list);
}

void test_tsort_fail(void)
{
  VList *list = get_test_list();
  VList *edges = get_faulty_test_edges();
  VList *sorted_list = tsort(list, &edges);

  CU_ASSERT_PTR_NULL(sorted_list);

  CU_ASSERT(edges != NULL);

  while (edges != NULL) {
    free(edges->data);
    edges = vlist_delete_link(edges, edges);
  }

  vlist_free(list);
}

CU_TestInfo tsort_tests[] = {
  { "test_tsort_succeed", test_tsort_succeed },
  { "test_tsort_fail", test_tsort_fail },
  CU_TEST_INFO_NULL,
};
