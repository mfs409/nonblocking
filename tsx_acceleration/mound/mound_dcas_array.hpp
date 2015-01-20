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
    // char padding[64 - sizeof(mound_word_t)];
};

/**
 * Mound is a tree of sorted lists, where the heads of the lists preserve the
 * min-heap invariant.
 */
class mound_dcas_t
{
    mound_dcas_node_t* tree;

    volatile uint32_t depth;

  public:
    /**
     * Constructor zeroes all.
     */
    mound_dcas_t()
    {
        tree = new mound_dcas_node_t[HEAP_MAX_SIZE];
        memset(tree, 0, sizeof(mound_dcas_node_t) * HEAP_MAX_SIZE);
        depth = 1;
    }

    ~mound_dcas_t()
    {
        delete [] tree;
    }

  private:
    /***  Indicate whether the given node is leaf. */
    inline bool is_leaf(uint32_t node)
    {
        uint32_t d = depth;
        CFENCE;
        uint32_t begin = 1 << (d - 1);
        uint32_t end = 1 << d;
        return begin <= node && node < end;
    }

    /***  Indicate whether the given node is root. */
    inline bool is_root(uint32_t node)
    {
        return node == 1;
    }

    /** Pick a node >= n*/
    uint32_t select_node(uint32_t n, mound_word_t * NN)
    {
        // [mfs] this now is much more close to the RANDOMIZATION_ON
        //       mound_seq code
        while (true) {
            // NB: seed is a contention hotspot!
            uint32_t index = rand_r_32(&seed);
            uint32_t d = depth;
            // use linear probing from a randomly selected point
            for (int i = 0; i < 8; ++i) {
                // make sure the number is between [2^(d-1), (2^d)-1]
                uint32_t N = ((index + i) % (1 << (d-1))) + (1 << (d-1));
                ATOMIC_READ(N, NN);
                mound_list_t* volatile LL = NN->fields.list;
                uint32_t nv = (LL == NULL) ? UINT_MAX : LL->data;
                // found a good node, so return
                if (nv >= n)
                    return N;
            }
            CFENCE;
            // if we failed too many times, grow the mound
            cas32(&depth, d, d + 1);
        }
    }

  private:
    inline void ATOMIC_READ(uint32_t N, mound_word_t * dest)
    {
        READ(&tree[N].word, dest);
    }

    inline bool ATOMIC_CAS(uint32_t N, mound_word_t NN, mound_word_t NN_new)
    {
#ifdef STM_CPU_X86
    return bcas64(&tree[N].word.all, NN.all, NN_new.all);
#else
    return bcas64_override(&tree[N].word.all, (unsigned long long*)&NN.all, (unsigned long long*)&NN_new.all);
#endif
    }

    inline bool ATOMIC_C2S1(uint32_t C, mound_word_t CC, mound_word_t CC_new,
                     uint32_t P, mound_word_t PP)
    {
        return C2S2(&tree[C].word, CC, CC_new, &tree[P].word, PP, PP);
    }

    inline bool ATOMIC_C2S2(uint32_t P, mound_word_t PP, mound_word_t PP_new,
                     uint32_t C, mound_word_t CC, mound_word_t CC_new)
    {
        return C2S2(&tree[C].word, CC, CC_new, &tree[P].word, PP, PP_new);
    }

  public:

    void add(uint32_t n)
    {
        uint32_t C, P, M;
        mound_word_t CC, PP, MM;
        uint32_t cdep, pdep, mdep;

        // [lyj] did not implement the optimizations in the mound_10_swap
        // algorithm, so if the atomic operations failed, we start over.
        while (true) {
            // pick a random leaf >= n (cache is written in CC)
            while (true) {
                // NB: seed is a contention hotspot!
                uint32_t index = rand_r_32(&seed);
                uint32_t d = depth;
                bool good = false;
                // use linear probing from a randomly selected point
                for (int i = 0; i < 64; ++i) {
                    // make sure the number is between [2^(d-1), (2^d)-1]
                    C = ((index + i) % (1 << (d-1))) + (1 << (d-1));
                    ATOMIC_READ(C, &CC);
                    uint32_t val = (CC.fields.list == NULL) ? UINT_MAX : CC.fields.list->data;
                    // found a good node, so return
                    if (val >= n) {
                        good = true;
                        cdep = d; // remember the depth of leaf
                        break;
                    }
                }
                if (good)
                    break;
                // if we failed too many times, grow the mound
                cas32(&depth, d, d + 1);
            }


            // P is initialized to root
            P = 1;
            pdep = 1;

            while (true) {
                // compute the level difference between C and P. we can achieve this by
                // counting the leading zeros.
                mdep = (cdep + pdep) / 2;
                // left shift C for harf of the different bits to get the middle
                M = (C >> (cdep - mdep));

                ATOMIC_READ(M, &MM);
                uint32_t mv = (MM.fields.list == NULL) ? UINT_MAX : MM.fields.list->data;

                if (n > mv) {
                    P = M;
                    PP.all = MM.all;
                    pdep = mdep;
                }
                else { // n <= mv
                    C = M;
                    CC.all = MM.all;
                    cdep = mdep;
                }

                // if M is root, break
                if (M == 1)
                    break;
                // if P is parent of C and P is not root, break
                if (P == C / 2 && P != 1)
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
        // start from the root
        uint32_t N = 1;
        mound_word_t NN;

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

    uint64_t fill_cavity(uint32_t N)
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
            uint32_t L = N * 2;
            uint32_t R = N * 2 + 1;
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
