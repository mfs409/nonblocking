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
 *  Sequential mound implementation
 */

#include "../common/platform.hpp"
#include "common.hpp"


/**
 *  Mound node: each node stores an ordered list implemented using a stack,
 *  which supports O(1) insert/remove
 */
struct mound_fc_node_t
{
    uint32_t       value;  // inlined value
    mound_list_t * list;   // local ordered list

    /*** peek is simple */
    inline uint32_t peek() { return value; }

    /*** push always puts its value at the head of the list */
    inline void push(uint32_t n)
    {
        //mound_list_t* cdr = mound_list_pool::get();
        mound_list_t* cdr = (mound_list_t*)malloc(sizeof(mound_list_t));
        // copy value/next to cdr
        cdr->data = value;
        cdr->next = list;
        // set new value/next
        value = n;
        list = cdr;
    }

    /*** remove first element from list */
    inline uint32_t pop()
    {
        uint32_t result = value;
        mound_list_t* cdr = list;
        if (list == NULL) {
            value = UINT_MAX;
            return result;
        }
        value = cdr->data;
        list = cdr->next;
        //mound_list_pool::put(cdr);
        free(cdr);
        return result;
    }
};




/**
 * Mound is a tree of sorted lists, where the heads of the lists preserve the
 * min-heap invariant.
 */
class mound_fc_t
{
    // node array
    mound_fc_node_t * nodes;

    // rightmost node
    uint32_t counter;

   public:

    /**
     * Constructor zeroes all but the first level, which has a single element
     */
    mound_fc_t()
    {
        counter = 1;
        nodes = new mound_fc_node_t[1048576];
        for (int i = 1; i < 1048576; i++) {
            nodes[i].value = UINT_MAX;
            nodes[i].list = NULL;
        }
    }

    ~mound_fc_t()
    {
        delete [] nodes;
    }

    /***  Add element to mound. */
    void add(uint32_t n)
    {
        while (true) {
            if (nodes[counter].peek() >= n)
                break;
            ++counter;
        }

        uint32_t pidx = 1;
        uint32_t plvl = 0;
        uint32_t cidx = counter;
        uint32_t clvl = 32 - __builtin_clz(counter);
        if (n <= nodes[1].peek()) {
            nodes[1].push(n);
        }
        else {
            while (plvl + 1 != clvl) {
                uint32_t mlvl = (clvl + plvl) / 2;
                uint32_t midx = cidx >> (clvl - mlvl);
                if (n > nodes[midx].peek()) {
                    plvl = mlvl;
                    pidx = midx;
                }
                else {
                    clvl = mlvl;
                    cidx = midx;
                }
            }
            nodes[cidx].push(n);
        }

        // // start looking at ancestors to figure out where we can insert this
        // // value
        // uint32_t clvl = bottom;
        // uint32_t cidx = leaf - levels[clvl];
        // while (clvl > 0) {
        //     uint32_t pidx = cidx / 2;
        //     uint32_t plvl = clvl - 1;
        //     if (n > levels[plvl][pidx].peek())
        //         break;
        //     clvl = plvl;
        //     cidx = pidx;
        // }
        // levels[clvl][cidx].push(n);

        return;
    }

    /***  Remove the minimum element from mound. */
    uint32_t remove()
    {
        // pop at root
        uint32_t result = nodes[1].pop();

        // Make sure list head's value is smaller than any child's list
        // head's value
        restore_invariants(1);

        return result;
    }

    /**
     *  Invoked by remove, and recursively
     *
     *  The invariant we are trying to re-establish is that a node's list's
     *  head cannot have a larger value than any of its children's lists'
     *  heads
     */
    void restore_invariants(uint32_t idx)
    {
        while (true) {
            // return if we reach the leaf node... it trivially supports the
            // invariant
            if (__builtin_clz(idx) == __builtin_clz(counter))
                return;

            // Figure out if we need to pull from a child

            // get my value
            uint32_t mine = nodes[idx].peek();

            // get left and right values
            uint32_t lidx = 2*idx;
            uint32_t ridx = lidx+1;
            uint32_t lv = nodes[lidx].peek();
            uint32_t rv = nodes[ridx].peek();

            // figure out which child is bigger
            uint32_t widx = lidx;
            uint32_t wv = lv;
            if (rv < lv) {
                widx = ridx;
                wv = rv;
            }

            // early exit if swap unnecessary
            if (wv >= mine)
                return;

            // // swap the lists of parent and child
            // mound_fc_node_t temp = nodes[widx];
            // nodes[widx] = nodes[idx];
            // nodes[idx] = temp;

            mound_list_t * cdr = nodes[widx].list;
            if (cdr) {
                // pop at child
                nodes[widx].value = cdr->data;
                nodes[widx].list = cdr->next;

                // reuse the "cdr" node
                cdr->data = mine;
                cdr->next = nodes[idx].list;

                // push at parent
                nodes[idx].value = wv;
                nodes[idx].list = cdr;
            }
            else {
                nodes[idx].push(wv);
                nodes[widx].value = UINT_MAX;
            }

            // move down a level
            idx = widx;
        }
    }
};


