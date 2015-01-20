#include "list.hpp"
#include "../common/locks.hpp"
#include "../common/ThreadLocal.hpp"

namespace {
/**
 * Per-thread node buffer reused in arrive function to avoid
 * memory allocation/deallocation overhead.
 */
THREAD_LOCAL_DECL_TYPE(sosillc_node_t) node_buffer;
}

void
sosillc_t::arrive(int32_t n)
{
    tatas_acquire(&lock);

    sosillc_node_t * left = &head;
    sosillc_node_t * right = head.next;
    while (right != &tail && right->value < n) {
        left = right;
        right = right->next;
    }

    sosillc_node_t* my_node_buffer = &node_buffer;

    my_node_buffer->value = n;
    left->next = my_node_buffer;
    right->prev = my_node_buffer;
    my_node_buffer->prev = left;
    my_node_buffer->next = right;

    if (head.next == my_node_buffer)
        min = n;

    CFENCE;
    tatas_release(&lock);
}

void
sosillc_t::depart()
{
    sosillc_node_t* my_node_buffer = &node_buffer;

    if (my_node_buffer->prev == NULL)
        return;

    tatas_acquire(&lock);

    sosillc_node_t * left = my_node_buffer->prev;
    sosillc_node_t * right = my_node_buffer->next;

    left->next = right;
    right->prev = left;

    if (min != head.next->value)
        min = head.next->value;

    CFENCE;
    tatas_release(&lock);

    my_node_buffer->prev = my_node_buffer->next = NULL;
}
