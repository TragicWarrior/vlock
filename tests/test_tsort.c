#include <stdlib.h>

#include <glib.h>

#include <CUnit/CUnit.h>

#include "tsort.h"

#include "test_tsort.h"

#define A ((void *)1)
#define B ((void *)2)
#define C ((void *)3)
#define D ((void *)4)
#define E ((void *)5)
#define F ((void *)6)
#define G ((void *)7)
#define H ((void *)8)

GList *get_test_list(void)
{
  GList *list = NULL;

  list = g_list_prepend(list, A);
  list = g_list_prepend(list, B);
  list = g_list_prepend(list, C);
  list = g_list_prepend(list, D);
  list = g_list_prepend(list, E);
  list = g_list_prepend(list, F);
  list = g_list_prepend(list, G);
  list = g_list_prepend(list, H);

  return list;
}

GList *get_test_edges(void)
{
  GList *edges = NULL;

  /* Edges:
   *
   *  E
   *  |
   *  B C D   H
   *   \|/    |
   *    A   F G
   */
  edges = g_list_append(edges, make_edge(A, B));
  edges = g_list_append(edges, make_edge(A, C));
  edges = g_list_append(edges, make_edge(A, D));
  edges = g_list_append(edges, make_edge(B, E));
  edges = g_list_append(edges, make_edge(G, H));

  return edges;  
}

GList *get_faulty_test_edges(void)
{
  GList *edges = NULL;

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

  edges = g_list_append(edges, make_edge(A, B));
  edges = g_list_append(edges, make_edge(A, C));
  edges = g_list_append(edges, make_edge(A, D));
  edges = g_list_append(edges, make_edge(B, E));
  edges = g_list_append(edges, make_edge(E, F));
  edges = g_list_append(edges, make_edge(F, A));
  edges = g_list_append(edges, make_edge(G, H));

  return edges;
}

void test_tsort_succeed(void)
{
  GList *list = get_test_list();
  GList *edges = get_test_edges();
  GList *sorted_list = tsort(list, &edges);

  CU_ASSERT_PTR_NULL(edges);

  CU_ASSERT_PTR_NOT_NULL(sorted_list);

  CU_ASSERT_EQUAL(g_list_length(list), g_list_length(sorted_list));

  /* Check that all items from the original list are in the sorted list. */
  for (GList *item = list; item != NULL; item = g_list_next(item))
    CU_ASSERT_PTR_NOT_NULL(g_list_find(sorted_list, item->data));

  /* Check that all items are in the order that is given by the edges. */
  edges = get_test_edges();

  while (edges != NULL) {
    struct edge *e = edges->data;
    CU_ASSERT(g_list_index(sorted_list, e->predecessor) < g_list_index(sorted_list, e->successor));
    free(e);
    edges = g_list_delete_link(edges, edges);
  }

  g_list_free(sorted_list);
  g_list_free(list);
}

void test_tsort_fail(void)
{
  GList *list = get_test_list();
  GList *edges = get_faulty_test_edges();
  GList *sorted_list = tsort(list, &edges);

  CU_ASSERT_PTR_NULL(sorted_list);

  CU_ASSERT(edges != NULL);

  while (edges != NULL) {
    free(edges->data);
    edges = g_list_delete_link(edges, edges);
  }

  g_list_free(list);
}

CU_TestInfo tsort_tests[] = {
  { "test_tsort_succeed", test_tsort_succeed },
  { "test_tsort_fail", test_tsort_fail },
  CU_TEST_INFO_NULL,
};
