#pragma once

#include <atomic>
#include <cassert>

#include "common.hpp"

using std::atomic;

/*** Node type for a list of timestamped void*s */
struct limbo_t
{
    /*** Number of void*s held in a limbo_t */
    static const uintptr_t POOL_SIZE   = 32;
    /*** Set of void*s */
    void*     pool[POOL_SIZE];
    /*** Timestamp when last void* was added */
    uintptr_t  ts[MAX_THREADS];
    /*** # valid timestamps in ts, or # elements in pool */
    uintptr_t  length;
    /*** NehelperMin pointer for the limbo list */
    limbo_t*  older;
    /*** The constructor for the limbo_t just zeroes out everything */
    limbo_t() : length(0), older(NULL) { }
};

// forward declaration
static void schedForReclaim(void* ptr);

// array of per thread timestamp counters
static pad_word_t                       trans_nums[MAX_THREADS];

// number of threads
static pad_word_t                       threadcount;

// thread id
static thread_local uintptr_t           my_id;

// pointer to my thread local counter
static thread_local atomic<uintptr_t> * my_ts;

/*** As we mark things for deletion, we accumulate them here */
static thread_local limbo_t *           prelimbo;

/*** sorted list of timestamped reclaimables */
static thread_local limbo_t *           limbo;


/** Initialize the memory manager. */
void wbmm_init(uintptr_t tn)
{
    threadcount.val = tn;
    for (uintptr_t i = 0; i < MAX_THREADS; i++) trans_nums[i].val = 0;
}

/** Initialize thread local data (called by each thread). */
void wbmm_thread_init(uintptr_t id)
{
    my_id = id;
    my_ts = &trans_nums[id].val;
    prelimbo = new limbo_t();
    limbo = NULL;
}

void * wbmm_alloc(size_t size)
{
    void * buf = malloc(size);
    assert (buf);
    return buf;
}

void wbmm_free_unsafe(void * ptr)
{
    free(ptr);
}

void wbmm_free_safe(void * ptr)
{
    schedForReclaim(ptr);
}

void wbmm_begin()
{
    *my_ts = *my_ts + 1;
}

void wbmm_end()
{
    *my_ts = *my_ts + 1;
}

inline uintptr_t wbmm_get_tid()
{
    return my_id;
}

inline uintptr_t wbmm_get_epoch()
{
    return *my_ts;
}

/*** figure out if one timestamp is strictly dominated by another */
static bool is_strictly_older(uintptr_t* newer, uintptr_t* older, uintptr_t old_len)
{
    for (uintptr_t i = 0; i < old_len; ++i)
        if ((newer[i] <= older[i]) && (newer[i] & 1))
            return false;
    return true;
}

/**
 *  This code is the cornerstone of the WBMMPolicy.  We buffer lots of
 *  frees onto a prelimbo list, and then, at some point, we must give
 *  that list a timestamp and tuck it away until the timestamp expires.
 *  This is how we do it.
 */
static void handle_full_prelimbo()
{
    // get the current timestamp from the epoch
    prelimbo->length = threadcount.val;

    for (uintptr_t i = 0, e = prelimbo->length; i < e; ++i)
        prelimbo->ts[i] = trans_nums[i].val;

    // push prelimbo onto the front of the limbo list:
    prelimbo->older = limbo;
    limbo = prelimbo;

    //  check if anything after limbo->head is dominated by ts.  Exit the loop
    //  when the list is empty, or when we find something that is strictly
    //  dominated.
    //
    //  NB: the list is in sorted order by timestamp.
    limbo_t* current = limbo->older;
    limbo_t* prev = limbo;
    while (current != NULL) {
        if (is_strictly_older(limbo->ts, current->ts, current->length))
            break;
        prev = current;
        current = current->older;
    }
    // If current != NULL, it is the head of a list of reclaimables
    if (current) {
        // detach /current/ from the list
        prev->older = NULL;
        // free all blocks in each node's pool and free the node
        while (current != NULL) {
            // free blocks in current's pool
            for (unsigned long i = 0; i < current->POOL_SIZE; i++)
                free(current->pool[i]);
            // free the node and move on
            limbo_t* old = current;
            current = current->older;
            free(old);
        }
    }
    prelimbo = new limbo_t();
}

/**
 *  Schedule a pointer for reclamation.  Reclamation will not happen
 *  until enough time has passed.
 */
static void schedForReclaim(void* ptr)
{
    // insert /ptr/ into the prelimbo pool and increment the pool size
    prelimbo->pool[prelimbo->length++] = ptr;
    // if prelimbo is not full, we're done
    if (prelimbo->length != prelimbo->POOL_SIZE)
        return;
    // if prelimbo is full, we have a lot more work to do
    handle_full_prelimbo();
}
