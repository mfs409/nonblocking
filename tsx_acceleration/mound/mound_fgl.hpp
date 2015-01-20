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

/**
 *  Fine-grained locking mound implementation
 */

#include <cstring>
#include <cassert>
#include "../common/platform.hpp"
#include "../common/locks.hpp"
#include "../alt-license/rand_r_32.h"

#include "common.hpp"

#define RAND_PLACEMENT

/**
 * Nodes in the Mound consist of a pointer to a list, a flag indicating if
 * the list is in an unusable state (store as the lowest bit of pointer).
 *
 *  [mfs] cache-line padding might really make a difference here...
 */
struct mound_fgl_node_t
{
    mound_list_t * volatile list;        // data word
    volatile uintptr_t       lock;        // per-node lock
};

/**
 * Mound is a tree of sorted lists, where the heads of the lists preserve the
 * min-heap invariant.
 */
class mound_fgl_t
{
    // We implement mound as an array of levels. Level i holds
    // 2^i elements.
    mound_fgl_node_t* levels[32];

    // The index of the level that currently is the leaves
    volatile uint32_t bottom;

#ifndef RAND_PLACEMENT
    // The counter points to the currently unused space in the level
    uint32_t counter;
#endif

    // per-mound lock for expanding
    volatile uint32_t mound_lock;

  public:
    /**
     * Constructor zeroes all but the first level, which has a single element
     */
    mound_fgl_t()
    {
        bottom = 0;
#ifdef RAND_PLACEMENT
        // seed = 0;
#else
        counter = 0;
#endif
        mound_lock = 0;

        levels[0] = new mound_fgl_node_t[1];
        memset(levels[0], 0, sizeof(mound_fgl_node_t));

        for (int i = 1; i < 32; i++)
            levels[i] = NULL;
    }

  private:
    /***  Indicate whether the given node is leaf. */
    inline bool is_leaf(mound_pos_t node)
    {
        return node.level == bottom;
    }

    /***  Indicate whether the given node is root. */
    inline bool is_root(mound_pos_t node)
    {
        return node.level == 0;
    }

    /*** Get the parent position of given node. */
    inline mound_pos_t parent_of(mound_pos_t node)
    {
        mound_pos_t result;
        result.level = node.level - 1;
        result.index = node.index / 2;
        return result;
    }

    /*** Get the left child position of given node. */
    inline mound_pos_t left_of(mound_pos_t node)
    {
        mound_pos_t result;
        result.level = node.level + 1;
        result.index = node.index * 2;
        return result;
    }

    /*** Get the right child position of given node. */
    inline mound_pos_t right_of(mound_pos_t node)
    {
        mound_pos_t result;
        result.level = node.level + 1;
        result.index = node.index * 2 + 1;
        return result;
    }

    /**
     * Extend an extra level for the mound.
     */
    __attribute__((noinline))
#ifdef RAND_PLACEMENT
    void grow(uint32_t btm)
#else
    void grow()
#endif
    {
#ifdef RAND_PLACEMENT
        // try to get the mound lock, so we can expand.  If bottom changes,
        // then stop trying to get the lock.  If get lock and then discover
        // change to bottom, just release lock and return.
        while (true) {
            if (bcas32(&mound_lock, 0, 1))
                break;
            if (bottom != btm)
                return;
            spin64();
        }
        if (bottom != btm) {
            mound_lock = 0;
            return;
        }
#endif
        // [lyj] expanding mound can be made lock-free using a CAS to
        //       increment the bottom variable, but we might not want
        //       multiple threads to allocate big arrays for the "same
        //       purpose" at the same time. Note that the lock holder does
        //       not prevent other threads from reading the bottom variable
        uint32_t size = 1 << (bottom + 1);
        mound_fgl_node_t* newlevel =
            (mound_fgl_node_t*)malloc(size * sizeof(mound_fgl_node_t));
        memset(newlevel, 0, size * sizeof(mound_fgl_node_t));
        CFENCE;
        levels[bottom + 1] = newlevel;
        CFENCE;
        // make the new level visible
        bottom++;
#ifdef RAND_PLACEMENT
        CFENCE;
        mound_lock = 0;
#endif
    }

    /** Pick a node >= n*/
    mound_pos_t select_node(uint32_t n)
    {
        // [mfs] this now is much more close to the RANDOMIZATION_ON
        //       mound_seq code
#ifdef RAND_PLACEMENT
        while (true) {
            // NB: seed is a contention hotspot!
            uint32_t index = rand_r_32(&seed);
            uint32_t b = bottom;
            int l = 8 * b; // [mfs] too big for very small trees
            // use linear probing from a randomly selected point
            for (int i = 0; i < l; ++i) {
                int ii = (index + i) % (1 << b);
                mound_pos_t N;
                N.level = b;
                N.index = ii;
                // found a good node, so return
                if (read_value(N) >= n) {
                    return N;
                }
                // stop probing if mound has been expanded
                if (b != bottom) break;
            }
            // if we failed too many times, grow the mound
            if (b == bottom)
                grow(b);
        }
#else
        tatas_acquire(&mound_lock);
        while (true) {

            // expand if we reach the last element
            if (counter == (1 << bottom)) {
                grow();
                counter = 0;
            }

            // peek the rightmost element at the bottem level
            mound_pos_t N;
            N.level = bottom;
            N.index = counter;
            // found a good node return
            if (read_value(N) >= n) {
                tatas_release(&mound_lock);
                return N;
            }
            ++counter;
        }
#endif
    }

  public:

    /***  Print the data structure for debugging. */
    void print()
    {
        mound_pos_t pos;
        pos.level = pos.index = 0;
        print_internal(pos);
    }

    /*** Internal method for printing the mound */
    void print_internal(mound_pos_t pos)
    {
        mound_list_t * curr = levels[pos.level][pos.index].list;

        if (curr == NULL)
            return;

        for (uint32_t i = 0; i < pos.level; i++)
            printf("  ");

        if (curr == NULL) {
            printf("nil");
        }
        while (curr) {
            printf("%d ", curr->data);
            curr = curr->next;
        }
        printf("\n");

        if (is_leaf(pos))
            return;

        print_internal(left_of(pos));
        print_internal(right_of(pos));
    }



  private:

    inline void swap_list(mound_pos_t a, mound_pos_t b)
    {
        mound_list_t * temp;
        temp = levels[a.level][a.index].list;
        levels[a.level][a.index].list = levels[b.level][b.index].list;
        levels[b.level][b.index].list = temp;
    }

    inline void lock_item(mound_pos_t pos)
    {
        tatas_acquire(&levels[pos.level][pos.index].lock);
    }

    inline void unlock_item(mound_pos_t pos)
    {
        tatas_release(&levels[pos.level][pos.index].lock);
    }

    inline uint32_t read_value(mound_pos_t pos)
    {
        mound_list_t * head = levels[pos.level][pos.index].list;
        return (head == NULL) ? UINT_MAX : head->data;
    }

    inline bool insert(mound_pos_t N, uint32_t num)
    {
        bool ans = false;

        // insert at root
        if (is_root(N)) {
            lock_item(N);
            uint32_t nv = read_value(N);
            if (num <= nv) {
                ans = true;
                mound_list_t * newlist = mound_list_pool::get();
                newlist->data = num;
                newlist->next = levels[N.level][N.index].list;
                levels[N.level][N.index].list = newlist;
            }
            unlock_item(N);
            return ans;
        }

        mound_pos_t P = parent_of(N);
        // insert at non-root
        lock_item(P);
        lock_item(N);
        uint32_t pv = read_value(P);
        uint32_t nv = read_value(N);
        if (num <= nv && num > pv) {
            ans = true;
            mound_list_t * newlist = mound_list_pool::get();
            newlist->data = num;
            newlist->next = levels[N.level][N.index].list;
            levels[N.level][N.index].list = newlist;
        }
        unlock_item(P);
        unlock_item(N);
        return ans;
    }

  public:

    void add(uint32_t n)
    {
        mound_pos_t C, P, M;

        while (true) {
            // pick a random leaf >= n (cache is written in CC)
            C = select_node(n);
            // P is initialized to root
            P.level = P.index = 0;

            while (true) {
                M.level = (C.level + P.level) / 2;
                M.index = C.index >> (C.level - M.level);
                uint32_t mv = read_value(M);

                if (n > mv)
                    P = M;
                else // n <= mv
                    C = M;

                if (M.level == 0)
                    break;
                if (P.level + 1 == C.level && P.level != 0)
                    break;
            }

            // push n on head of root
            if (insert(C, n))
                return;
        }
    }

    uint32_t remove()
    {
        // start from the root
        mound_pos_t N;
        N.level = N.index = 0;

        // lock the root
        lock_item(N);

        // if root is null, return top
        mound_list_t * head = levels[N.level][N.index].list;
        if (head == NULL) {
            unlock_item(N);
            return UINT_MAX;
        }

        // retireve element from root
        uint32_t ret = head->data;
        levels[N.level][N.index].list = head->next;
        mound_list_pool::put(head);

        // The following loop restores the invariant
        // PRECONDITION::
        //    thread holds only the lock of parent node N
        while (true) {
            // if N is leaf, we are done
            if (is_leaf(N)) {
                unlock_item(N);
                return ret;
            }

            // acquire locks of left and right
            mound_pos_t L = left_of(N);
            mound_pos_t R = right_of(N);
            lock_item(L);
            lock_item(R);

            // now we are holding the locks of N, L, R
            uint32_t nv = read_value(N);
            uint32_t lv = read_value(L);
            uint32_t rv = read_value(R);

            // pull from right?
            if ((rv <= lv) && (rv < nv)) {
                swap_list(R, N);
                // release the lock of parent and left, re adjust right
                unlock_item(N);
                unlock_item(L);
                // now we are holding the locks of R, in next iteration it
                // will become N.
                N = R;
            }
            // pull from left?
            else if ((lv <= rv) && (lv < nv)) {
                swap_list(L, N);
                // release the lock of parent and right, re adjust left
                unlock_item(N);
                unlock_item(R);
                // now we are holding the locks of L, in next iteration it
                // will become N.
                N = L;
            }
            // pull from local list? just clear the cavity and we're good
            else {
                // release all locks and return
                unlock_item(N);
                unlock_item(L);
                unlock_item(R);
                return ret;
            }
        }

        return ret;
    }

    mound_list_t* removeMany()
    {
        // start from the root
        mound_pos_t N;
        N.level = N.index = 0;

        // lock the root
        lock_item(N);

        // if root is null, return top
        mound_list_t * head = levels[N.level][N.index].list;
        if (head == NULL) {
            unlock_item(N);
            return NULL;
        }

        // retreive element from root
        mound_list_t* ret = head;
        levels[N.level][N.index].list = NULL;
        // mound_list_pool::put(head);

        // The following loop restores the invariant
        // PRECONDITION::
        //    thread holds only the lock of parent node N
        while (true) {
            // if N is leaf, we are done
            if (is_leaf(N)) {
                unlock_item(N);
                return ret;
            }

            // acquire locks of left and right
            mound_pos_t L = left_of(N);
            mound_pos_t R = right_of(N);
            lock_item(L);
            lock_item(R);

            // now we are holding the locks of N, L, R
            uint32_t nv = read_value(N);
            uint32_t lv = read_value(L);
            uint32_t rv = read_value(R);

            // pull from right?
            if ((rv <= lv) && (rv < nv)) {
                swap_list(R, N);
                // release the lock of parent and left, re adjust right
                unlock_item(N);
                unlock_item(L);
                // now we are holding the locks of R, in next iteration it
                // will become N.
                N = R;
            }
            // pull from left?
            else if ((lv <= rv) && (lv < nv)) {
                swap_list(L, N);
                // release the lock of parent and right, re adjust left
                unlock_item(N);
                unlock_item(R);
                // now we are holding the locks of L, in next iteration it
                // will become N.
                N = L;
            }
            // pull from local list? just clear the cavity and we're good
            else {
                // release all locks and return
                unlock_item(N);
                unlock_item(L);
                unlock_item(R);
                return ret;
            }
        }

        return ret;
    }

};
