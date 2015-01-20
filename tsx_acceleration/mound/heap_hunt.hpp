///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2011
// Lehigh University
// Computer Science and Engineering Department
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the University of Rochester nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/** Priority queue implementation using hunt heap. */

#include "../common/platform.hpp"
#include "common.hpp"

#define BIT_REVERSED 1
#define ENABLE_YIELD 1
//#define VOLATILE_IF_DEBUG volatile
#define VOLATILE_IF_DEBUG

#define LOCK(x) tatas_acquire(&items[x].lock)
#define UNLOCK(x) tatas_release(&items[x].lock)
#define LOCK_HEAP() tatas_acquire(&this->lock)
#define UNLOCK_HEAP() tatas_release(&this->lock)
#define TAG(x) items[x].tag
#define PRIORITY(x) items[x].priority

enum data_item_tag_t
    {
        EMPTY = 0,
        AVAILABLE,
    };


struct data_item_t
{
    volatile uintptr_t lock;
    volatile uint32_t tag;
    volatile uint32_t priority;
};

class heap_hunt_t
{
  private:
    volatile uintptr_t lock;
#if BIT_REVERSED
    volatile int32_t  counter;
    volatile int32_t  reverse;
    volatile int32_t  highbit;
#else
    volatile uint32_t size;
#endif
    data_item_t *     items;

  public:
    heap_hunt_t()
    {
        lock = 0;

#if BIT_REVERSED
        counter = 0;
        reverse = 0;
        highbit = 0;
        bit_reversed_increment();
#else
        size = 1;
#endif
        items = new data_item_t[HEAP_MAX_SIZE + 1];
        memset(items, 0, sizeof(data_item_t) * (HEAP_MAX_SIZE + 1));
    }

    ~heap_hunt_t()
    {
        delete [] items;
    }

#if BIT_REVERSED
    inline uint32_t bit_reversed_increment()
    {
        if (counter++ == 0) {
            reverse = highbit = 1;
            return reverse;
        }
        int32_t bit = highbit >> 1;
        while (bit != 0) {
            reverse ^= bit;
            if ((reverse & bit) != 0)
                break;
            bit >>= 1;
        }
        if (bit == 0)
            reverse = highbit <<= 1;
        return reverse;
    }

    inline uint32_t bit_reversed_decrement()
    {
        counter--;
        int32_t bit = highbit >> 1;
        while (bit != 0) {
            reverse ^= bit;
            if ((reverse & bit) == 0)
                break;
            bit >>= 1;
        }
        if (bit == 0) {
            reverse = counter;
            highbit >>= 1;
        }
        return reverse;
    }
#endif

    inline void swap_items(uint32_t a, uint32_t b)
    {
        data_item_t temp;
        temp.tag = items[a].tag;
        temp.priority = items[a].priority;
        items[a].tag = items[b].tag;
        items[a].priority = items[b].priority;
        items[b].tag = temp.tag;
        items[b].priority = temp.priority;
    }

    inline void swap(volatile uint32_t & a, volatile uint32_t & b)
    {
        uint32_t temp;
        temp = a;
        a = b;
        b = temp;
    }

    void add(uint32_t priority)
    {
        // reserve some small integers for special tags
        uint32_t pid = (uint32_t)pthread_self() + 10;

        // Insert new item at bottom of the heap.
        LOCK_HEAP();
#if BIT_REVERSED
        VOLATILE_IF_DEBUG uint32_t i = reverse;
        bit_reversed_increment();
#else
        VOLATILE_IF_DEBUG uint32_t i = size++;
#endif
        LOCK(i);
        UNLOCK_HEAP();
        PRIORITY(i) = priority;
        TAG(i) = pid;
        UNLOCK(i);

        // Move item towards top of heap while it has a higher priority
        //   than its parent.
        while (i > 1) {
            VOLATILE_IF_DEBUG uint32_t parent = i / 2;
            LOCK(parent);
            LOCK(i);
            VOLATILE_IF_DEBUG uint32_t old_i = i;

            bool potential_livelock = false;

            if (TAG(parent) == AVAILABLE && TAG(i) == pid) {
                if (PRIORITY(i) < PRIORITY(parent)) {
                    swap_items(i, parent);
                    i = parent;
                }
                else {
                    TAG(i) = AVAILABLE;
                    i = 0;
                }
            }
            else if (TAG(parent) == EMPTY) {
                i = 0;
            }
            else if (TAG(i) != pid) {
                i = parent;
            }
            else {
                potential_livelock = true;
            }

            UNLOCK(old_i);
            UNLOCK(parent);

#if ENABLE_YIELD
            if (potential_livelock)
                sched_yield();
#endif
        }
        if (i == 1) {
            LOCK(i);
            if (TAG(i) == pid) {
                TAG(i) = AVAILABLE;
            }
            UNLOCK(i);
        }
    }

    uint32_t remove()
    {
        // Grab an item from the bottom of the heap to replace the
        //   to-be-deleted top item.
        LOCK_HEAP();

        // if counter points to root, then the heap is empty
#if BIT_REVERSED
        if (reverse == 1) {
#else
        if (size == 1) {
#endif
            tatas_release(&lock);
            return UINT_MAX;
        }
        // otherwise, counter > 1

#if BIT_REVERSED
        bit_reversed_decrement();
        VOLATILE_IF_DEBUG uint32_t bottom = reverse;
#else
        VOLATILE_IF_DEBUG uint32_t bottom = --size;
#endif
        LOCK(bottom);
        UNLOCK_HEAP();
        VOLATILE_IF_DEBUG uint32_t priority = PRIORITY(bottom);
        TAG(bottom) = EMPTY;
        UNLOCK(bottom);

        // Lock first item.  Stop if it was the only item in the heap.
        LOCK(1);
        if (TAG(1) == EMPTY) {
            UNLOCK(1);
            return priority;
        }

        // Replace the top item with the item stored from the bottom.
        swap(priority, PRIORITY(1));
        TAG(1) = AVAILABLE;

        // Adjust the heap starting at the top.
        //   We always hold a lock on the item being adjusted.
        VOLATILE_IF_DEBUG uint32_t i = 1;
        while (i < HEAP_MAX_SIZE / 2) {
            VOLATILE_IF_DEBUG uint32_t left = i * 2;
            VOLATILE_IF_DEBUG uint32_t right = i * 2 + 1;
            VOLATILE_IF_DEBUG uint32_t child;

            LOCK(left);
            LOCK(right);

            if (TAG(left) == EMPTY) {
                UNLOCK(right);
                UNLOCK(left);
                break;
            }
            else if (TAG(right) == EMPTY || PRIORITY(left) < PRIORITY(right)) {
                UNLOCK(right);
                child = left;
            }
            else {
                UNLOCK(left);
                child = right;
            }

            // If the child has a higher priority than the parent then
            //   swap them.  If not, stop.
            if (PRIORITY(child) < PRIORITY(i)) {
                swap_items(child, i);
                UNLOCK(i);
                i = child;
            }
            else {
                UNLOCK(child);
                break;
            }
        }

        UNLOCK(i);
        return priority;
    }
};

