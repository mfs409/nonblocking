#include "skiplist.hpp"
#include "fraser.hpp"
#include "../common/ThreadLocal.hpp"


/** Per-thread value buffer. */
THREAD_LOCAL_DECL_TYPE(int32_t) val_buffer;

/** Seed for skip list. */
THREAD_LOCAL_DECL_TYPE(uint32_t) fraser_seed = 0;

volatile uint32_t sl_counter = 0;


skiplist_t::skiplist_t()
{
    slset = sl_set_new();
}

int32_t
skiplist_t::query()
{
    sl_node_t * curr = slset->head->nexts[0];
    return curr->val;
}

void
skiplist_t::arrive(int32_t, int32_t n)
{
    val_buffer = n;
    fraser_insert(slset, n, false);
}

void
skiplist_t::depart(int32_t)
{
    int32_t n = val_buffer;
    while (!fraser_remove(slset, n));
}
