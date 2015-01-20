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
 *  Lock-free DCAS-based mound implementation
 */

#include <cstring>
#include <cassert>
#include "../common/platform.hpp"
#include "../alt-license/rand_r_32.h"

#include "common.hpp"

#define RAND_PLACEMENT

/**
 * Nodes in the Mound consist of a pointer to a list, a flag indicating if
 * the list is in an unusable state (store as the lowest bit of pointer).
 *
 *  [mfs] cache-line padding might really make a difference here...
 */
struct mound_dcas_node_t
{
    mound_word_t word;        // data word
    uint32_t     lock;        // lock
    // char padding[64 - sizeof(mound_word_t)];
};

/**
 * Mound is a tree of sorted lists, where the heads of the lists preserve the
 * min-heap invariant.
 */
class mound_dcas_t
{
    // We implement mound as an array of levels. Level i holds
    // 2^i elements.
    mound_dcas_node_t* levels[32];

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
    mound_dcas_t()
    {
        bottom = 0;
#ifdef RAND_PLACEMENT
        // seed = 0;
#else
        counter = 0;
#endif
        mound_lock = 0;

        levels[0] = new mound_dcas_node_t[1];
        levels[0][0].word.all = 0;
        levels[0][0].lock = 0;

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
        mound_dcas_node_t* newlevel =
            (mound_dcas_node_t*)malloc(size * sizeof(mound_dcas_node_t));
        memset(newlevel, 0, size * sizeof(mound_dcas_node_t));
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
    mound_pos_t select_node(uint32_t n, mound_word_t * NN)
    {
        // [mfs] this now is much more close to the RANDOMIZATION_ON
        //       mound_seq code
#ifdef RAND_PLACEMENT
        while (true) {
            // NB: seed is a contention hotspot!
            uint32_t index = rand_r_32(&seed);
            uint32_t b = bottom;
            int l = 8; // [mfs] too big for very small trees
            // use linear probing from a randomly selected point
            for (int i = 0; i < l; ++i) {
                int ii = (index + i) % (1 << b);
                mound_pos_t N;
                N.level = b;
                N.index = ii;
                ATOMIC_READ(N, NN);
                // [mfs] this isn't safe... the list node could have moved,
                //       and we never locked it.  consider seqlock
                mound_list_t* volatile LL = NN->fields.list;
                uint32_t nv = (LL == NULL) ? UINT_MAX : LL->data;

                // found a good node, so return
                if (nv >= n) {
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
            ATOMIC_READ(N, NN);
            uint32_t nv =
                (NN->fields.list == NULL) ? UINT_MAX : NN->fields.list->data;

            // found a good node return
            if (nv >= n) {
                tatas_release(&mound_lock);
                return N;
            }
            ++counter;
        }
#endif
    }

  private:
    inline void ATOMIC_READ(mound_pos_t pos, mound_word_t * dest)
    {
        mvx(&levels[pos.level][pos.index].word.all, &dest->all);
    }

    inline bool ATOMIC_CAS(mound_pos_t N, mound_word_t NN, mound_word_t NN_new)
    {
        mound_dcas_node_t & node = levels[N.level][N.index];
        bool ans = false;
        tatas_acquire(&node.lock);
        if (node.word.fields.version == NN.fields.version) {
            mvx(&NN_new.all, &node.word.all);
            ans = true;
        }
        tatas_release(&node.lock);
        return ans;
    }

    inline bool ATOMIC_C2S1(mound_pos_t C, mound_word_t CC, mound_word_t CC_new,
                     mound_pos_t P, mound_word_t PP)
    {
        mound_dcas_node_t & child = levels[C.level][C.index];
        mound_dcas_node_t & parent = levels[P.level][P.index];
        bool ans = false;
        tatas_acquire(&child.lock);
        tatas_acquire(&parent.lock);
        if (child.word.fields.version == CC.fields.version
           && parent.word.fields.version == PP.fields.version) {
            mvx(&CC_new.all, &child.word.all);
            ans = true;
        }
        tatas_release(&child.lock);
        tatas_release(&parent.lock);
        return ans;
    }

    inline bool ATOMIC_C2S2(mound_pos_t P, mound_word_t PP, mound_word_t PP_new,
                     mound_pos_t C, mound_word_t CC, mound_word_t CC_new)
    {
        mound_dcas_node_t & child = levels[C.level][C.index];
        mound_dcas_node_t & parent = levels[P.level][P.index];
        bool ans = false;
        tatas_acquire(&child.lock);
        tatas_acquire(&parent.lock);
        if (child.word.fields.version == CC.fields.version
           && parent.word.fields.version == PP.fields.version) {
            mvx(&CC_new.all, &child.word.all);
            mvx(&PP_new.all, &parent.word.all);
            ans = true;
        }
        tatas_release(&child.lock);
        tatas_release(&parent.lock);
        return ans;
    }

  public:

    void add(uint32_t n)
    {
        mound_pos_t C, P, M;
        mound_word_t CC, PP, MM;

        // [lyj] did not implement the optimizations in the mound_10_swap
        // algorithm, so if the atomic operations failed, we start over.
        while (true) {
            // pick a random leaf >= n (cache is written in CC)
            C = select_node(n, &CC);
            // P is initialized to root
            P.level = P.index = 0;

            while (true) {
                // [lyj] We don't care about whether the node is cavity or
                //       not, we only care about their values. on the other
                //       hand, the following atomic update operations keep
                //       the cavity field unchanged, which is critical to
                //       maintain invariants.
                M.level = (C.level + P.level) / 2;
                M.index = C.index >> (C.level - M.level);
                ATOMIC_READ(M, &MM);
                uint32_t mv = (MM.fields.list == NULL) ? UINT_MAX : MM.fields.list->data;

                if (n > mv) {
                    P = M;
                    PP.all = MM.all;
                }
                else { // n <= mv
                    C = M;
                    CC.all = MM.all;
                }

                if (M.level == 0)
                    break;
                if (P.level + 1 == C.level && P.level != 0)
                    break;
            }

            // prepare to insert a new node on head of child
            mound_list_t * newlist = mound_list_pool::get();
            newlist->data = n;
            newlist->next = CC.fields.list;
            mound_word_t CC_new;
            MAKE_WORD(CC_new, newlist, CC.fields.cavity, CC.fields.version+1);

            // push n on head of root (need to ensure C <= n)
            if (is_root(C)) {
                if (ATOMIC_CAS(C, CC, CC_new))
                    return;
            }
            // push n on child (need to ensure C <= n < P)
            else if (ATOMIC_C2S1(C, CC, CC_new, P, PP)) { // no change on P
                return;
            }
        }
    }

    uint32_t remove()
    {
        mound_pos_t N;
        mound_word_t NN;

        // start from the root
        N.level = N.index = 0;

        while (true) {
            // if root is cavity, fill it first
            ATOMIC_READ(N, &NN);
            if (NN.fields.cavity) {
                NN.all = fill_cavity(N);
            }

            if (NN.fields.list == NULL)
                return UINT_MAX;  // SAFE: single read

            // retrieve the value from root
            mound_word_t tmp;
            MAKE_WORD(tmp, NN.fields.list->next, true, NN.fields.version + 1);
            if (ATOMIC_CAS(N, NN, tmp)) {
                int ret = NN.fields.list->data;
                mound_list_pool::put(NN.fields.list);
                fill_cavity(N);
                return ret;
            }
        }
    }

    mound_list_t* removeMany()
    {
        mound_pos_t N;
        mound_word_t NN;

        // start from the root
        N.level = N.index = 0;

        while (true) {
            // if root is cavity, fill it first
            ATOMIC_READ(N, &NN);
            if (NN.fields.cavity) {
                NN.all = fill_cavity(N);
            }

            if (NN.fields.list == NULL)
                return NULL;  // SAFE: single read

            // retrieve the list from root
            mound_word_t tmp;
            MAKE_WORD(tmp, NULL, true, NN.fields.version + 1);
            if (ATOMIC_CAS(N, NN, tmp)) {
                mound_list_t* ret = NN.fields.list;
                // mound_list_pool::put(NN.fields.list);
                fill_cavity(N);
                return ret;
            }
        }
    }


    uint64_t fill_cavity(mound_pos_t N)
    {
        // for caching timestamps etc
        mound_word_t NN, LL, RR;

        // note that we do not support reducing the size of a mound, so if it
        // isn't a leaf, we don't need atomicity between the check and
        // subsequent ops.  Furthermore, the check can be outside the
        // /while/, because this isn't going to become a leaf.  However, this
        // could stop being a leaf, so the check needs to be atomic wrt
        // expanding the mound.

        // we don't care about cavity leaves
        if (is_leaf(N)) {
            ATOMIC_READ(N, &NN);
            return NN.all;
        }

        while (true) {
            // return if N is already not a cavity
            ATOMIC_READ(N, &NN);
            if (!NN.fields.cavity)
                return NN.all;

            // Now comes the hard work.
            mound_pos_t L = left_of(N);
            mound_pos_t R = right_of(N);
            // values
            uint32_t nv, rv, lv;

            // ensure left is not cavity, otherwise fill it
            ATOMIC_READ(L, &LL);
            if (LL.fields.cavity) {
                LL.all = fill_cavity(L);
            }
            // ensure right is not cavity, otherwise fill it
            ATOMIC_READ(R, &RR);
            if (RR.fields.cavity) {
                RR.all = fill_cavity(R);
            }

            // compute the minimum value
            nv = (NN.fields.list == NULL) ? UINT_MAX : NN.fields.list->data;
            lv = (LL.fields.list == NULL) ? UINT_MAX : LL.fields.list->data;
            rv = (RR.fields.list == NULL) ? UINT_MAX : RR.fields.list->data;

            // pull from right?
            if ((rv <= lv) && (rv < nv)) {
                // swap R and N lists
                mound_word_t NN_new, RR_new;
                MAKE_WORD(NN_new, RR.fields.list, false, NN.fields.version + 1);
                MAKE_WORD(RR_new, NN.fields.list, true,  RR.fields.version + 1);
                if (ATOMIC_C2S2(N, NN, NN_new, R, RR, RR_new)) {
                    fill_cavity(R);
                    return NN_new.all;
                }
            }
            // pull from left?
            else if ((lv <= rv) && (lv < nv)) {
                // swap L and N lists
                mound_word_t NN_new, LL_new;
                MAKE_WORD(NN_new, LL.fields.list, false, NN.fields.version + 1);
                MAKE_WORD(LL_new, NN.fields.list, true,  LL.fields.version + 1);
                if (ATOMIC_C2S2(N, NN, NN_new, L, LL, LL_new)) {
                    fill_cavity(L);
                    return NN_new.all;
                }
            }
            // pull from local list?
            // just clear the cavity and we're good
            else {
                mound_word_t NN_new;
                MAKE_WORD(NN_new, NN.fields.list, false, NN.fields.version + 1);
                if (ATOMIC_CAS(N, NN, NN_new))
                    return NN_new.all;
            }
#ifdef STM_CPU_X86
            for (uint32_t i = 0; i < 64; i++) spin64();
#endif
        }
    }
};
