///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2014
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
 *  RTM-coarse-grained implementation
 */

#include <cstring>
#include <cassert>
#include "../common/platform.hpp"
#include "../alt-license/rand_r_32.h"
#include "common.hpp"

#define DIAGNOSTIC_MESSAGES
#define RANDOMIZATION_ON

/**
 *  Mound node: each node stores an ordered list implemented using a stack,
 *  which supports O(1) insert/remove
 */
struct mound_cgl_node_t
{
    mound_list_t* list;   // local ordered list
    
    /*** since all data is in the list, a null list means an empty node */
    inline bool empty() { return list == NULL; }
    
    /*** peek is simple */
    inline uint32_t peek() { return (list == NULL) ? UINT_MAX : list->data; }
    
    /*** push always puts its value at the head of the list */
    inline void push(uint32_t n)
    {
        mound_list_t* head = mound_list_pool::get();
        head->data = n;
        head->next = list;
        list = head;
    }
    
    /*** remove first element from list */
    inline uint32_t pop()
    {
        if (list == NULL)
            return UINT_MAX;
        mound_list_t* head = list;
        uint32_t result = head->data;
        list = head->next;
        mound_list_pool::put(head);
        return result;
    }
};

/**
 * Mound is a tree of sorted lists, where the heads of the lists preserve the
 * min-heap invariant.
 */
class mound_RTM_cgl_t
{
    // We implement mound as an array of levels. Level i holds
    // 2^i elements.
    mound_cgl_node_t* levels[32];
    
    // The index of the level that currently is the leaves
    uint32_t bottom;
    
    // The counter points to the currently unused space in the level
    uint32_t counter;
    
    // seed for random number generator
    uint32_t seed;
    
    // lock
    volatile uintptr_t lock;
    
public:
    
    /**
     * Constructor zeroes all but the first level, which has a single element
     */
    mound_RTM_cgl_t()
    {
        bottom = 0;
        counter = 0;
        seed = 0;
        
        lock = 0;
        
        levels[0] = new mound_cgl_node_t[1];
        levels[0][0].list = NULL;
        
        for (int i = 1; i < 32; i++)
            levels[i] = NULL;
    }
    // return true if locked
    bool isLocked()
    {
        return (lock != 0);
    }
    /***  Add element to mound. */
    void add(uint32_t n)
    {
        uint32_t status;
        
        status = _xbegin();
        if(status == _XBEGIN_STARTED)
        {
            if(isLocked())
                _xabort(6);
            mound_cgl_node_t* leaf = NULL;
            
#ifdef RANDOMIZATION_ON
            while (true) {
                // make a few attempts, based on the size of the leaf set
                uint32_t index = rand_r_32(&seed);
                int b = bottom;
                int l = 8*b; // [mfs] too big for very small trees
                // use linear probing from a randomly selected point
                for (int i = 0; i < l; ++i) {
                    int ii = (index + i) % (1 << b);
                    mound_cgl_node_t* tmp = &levels[b][ii];
                    if (tmp->peek() >= n) {
                        leaf = tmp;
                        break;
                    }
                }
                // if we found a good node, we exit this loop
                if (leaf)
                    break;
                // otherwise, WHP the leaves are full, so we need to rebuild
                grow();
                // [mfs] This is probably too much cheating
                leaf = &levels[bottom][(index % (1<<bottom))];
                break;
            }
#else
            while (true) {
                if (counter == (1 << bottom)) {
                    grow();
                    counter = 0;
                }
                if (levels[bottom][counter].peek() >= n) {
                    leaf = &levels[bottom][counter];
                    break;
                }
                ++counter;
            }
#endif
            uint32_t clvl = bottom;
            uint32_t cidx = leaf - levels[clvl];
            uint32_t plvl = 0, pidx = 0;
            if (n <= levels[0][0].peek()) {
                levels[0][0].push(n);
            }
            else {
                while (plvl + 1 != clvl) {
                    uint32_t mlvl = (clvl + plvl) / 2;
                    uint32_t midx = cidx >> (clvl - mlvl);
                    if (n > levels[mlvl][midx].peek()) {
                        plvl = mlvl;
                        pidx = midx;
                    }
                    else {
                        clvl = mlvl;
                        cidx = midx;
                    }
                }
                levels[clvl][cidx].push(n);
            }
            _xend();
        }
        else
        {
            tatas_acquire(&lock);
            
            // step one: find a leaf.  It may take multiple attempts.
            mound_cgl_node_t* leaf = NULL;
            
#ifdef RANDOMIZATION_ON
            while (true) {
                // make a few attempts, based on the size of the leaf set
                uint32_t index = rand_r_32(&seed);
                int b = bottom;
                int l = 8*b; // [mfs] too big for very small trees
                // use linear probing from a randomly selected point
                for (int i = 0; i < l; ++i) {
                    int ii = (index + i) % (1 << b);
                    mound_cgl_node_t* tmp = &levels[b][ii];
                    if (tmp->peek() >= n) {
                        leaf = tmp;
                        break;
                    }
                }
                
                // if we found a good node, we exit this loop
                if (leaf)
                    break;
                
                // otherwise, WHP the leaves are full, so we need to rebuild
                grow();
                // [mfs] This is probably too much cheating
                leaf = &levels[bottom][(index % (1<<bottom))];
                break;
            }
#else
            while (true) {
                if (counter == (1 << bottom)) {
                    grow();
                    counter = 0;
                }
                if (levels[bottom][counter].peek() >= n) {
                    leaf = &levels[bottom][counter];
                    break;
                }
                ++counter;
            }
#endif
            uint32_t clvl = bottom;
            uint32_t cidx = leaf - levels[clvl];
            uint32_t plvl = 0, pidx = 0;
            if (n <= levels[0][0].peek()) {
                levels[0][0].push(n);
            }
            else {
                while (plvl + 1 != clvl) {
                    uint32_t mlvl = (clvl + plvl) / 2;
                    uint32_t midx = cidx >> (clvl - mlvl);
                    if (n > levels[mlvl][midx].peek()) {
                        plvl = mlvl;
                        pidx = midx;
                    }
                    else {
                        clvl = mlvl;
                        cidx = midx;
                    }
                }
                levels[clvl][cidx].push(n);
            }
            
            tatas_release(&lock);
        }
        
        return;
    }
    
    /***  Remove the minimum element from mound. */
    uint32_t remove()
    {
        uint32_t status;
        uint32_t result;
        status = _xbegin();
        if(status == _XBEGIN_STARTED)
        {
            if(isLocked())
                _xabort(6);
            result = levels[0][0].pop();
            restore_invariants(0, 0);
            _xend();
        }else
        {
            tatas_acquire(&lock);
            // pop at root
            result = levels[0][0].pop();
            
            // Make sure list head's value is smaller than any child's list
            // head's value
            restore_invariants(0, 0);
            
            tatas_release(&lock);
        }
        return result;
    }
    
    /***  Print the data structure for debugging. */
    void print()
    {
        print_internal(get_root(), 0);
    }
    
    /*** Internal method for printing the mound */
    void print_internal(mound_cgl_node_t * node, int depth)
    {
        mound_list_t * curr = node->list;
        
        if (curr == NULL)
            return;
        
        for (int i = 0; i < depth; i++)
            printf("  ");
        
        if (curr == NULL) {
            printf("nil");
        }
        while (curr) {
            printf("%d ", curr->data);
            curr = curr->next;
        }
        printf("\n");
        
        if (is_leaf(node))
            return;
        
        print_internal(get_children(node), depth + 1);
        print_internal(get_children(node) + 1, depth + 1);
    }
    
    void print_average_list_size()
    {
        for (uint32_t i = 0; i < bottom; i++)
            std::cout << "level " << i << ", avg list size = "
            << average_list_size(i) << std::endl;
    }
    
    void print_average_list_priority()
    {
        for (uint32_t i = 0; i < bottom; i++)
            std::cout << "level " << i << ", avg priority = "
            << average_list_priority(i) << std::endl;
    }
    
    double average_list_size(const uint32_t lvl)
    {
        double total = 0;
        uint32_t num = 1 << lvl;
        for (uint32_t i = 0; i < num; i++)
            total += get_list_size(lvl, i);
        return total / num;
    }
    
    double average_list_priority(const uint32_t lvl)
    {
        double total = 0;
        uint64_t size = 0;
        uint32_t num = 1 << lvl;
        for (uint32_t i = 0; i < num; i++) {
            total += get_list_total(lvl, i);
            size += get_list_size(lvl, i);
        }
        if (size == 0)
            return UINT_MAX;
        return total / size;
    }
    
    uint64_t get_list_total(const uint32_t lvl, const uint32_t idx)
    {
        mound_cgl_node_t * node = &levels[lvl][idx];
        mound_list_t * curr = node->list;
        uint64_t total = 0;
        while (curr) {
            total += curr->data;
            curr = curr->next;
        }
        return total;
    }
    
    uint32_t get_list_size(const uint32_t lvl, const uint32_t idx)
    {
        mound_cgl_node_t * node = &levels[lvl][idx];
        mound_list_t * curr = node->list;
        uint32_t count = 0;
        while (curr) {
            curr = curr->next;
            count++;
        }
        return count;
    }
    
    
private:
    uint32_t get_level(mound_cgl_node_t* n)
    {
        int b = bottom;
        for (int i = b; i >= 0; --i) {
            mound_cgl_node_t* first = &levels[i][0];
            mound_cgl_node_t* last = &levels[i][1<<i];
            if ((n >= first) && (n < last))
                return i;
        }
        // big trouble here...
        return -1;
    }
    
    /***  Get the root node. */
    mound_cgl_node_t * get_root() { return levels[0]; }
    
    /***  Get the first child of given node. */
    mound_cgl_node_t * get_children(mound_cgl_node_t * node)
    {
        int lvl = get_level(node);
        int idx = node - levels[lvl];
        return &levels[lvl + 1][idx * 2];
    }
    
    /***  Indicate whether the given node is leaf. */
    bool is_leaf(mound_cgl_node_t * node)
    {
        return get_level(node) == bottom;
    }
    
    /**
     *  Invoked by remove, and recursively
     *
     *  The invariant we are trying to re-establish is that a node's list's
     *  head cannot have a larger value than any of its children's lists'
     *  heads
     */
    void restore_invariants(uint32_t _lvl, uint32_t _idx)
    {
        uint32_t lvl = _lvl;
        uint32_t idx = _idx;
        while (true) {
            // return if we reach the leaf node... it trivially supports the
            // invariant
            if (lvl == bottom)
                return;
            
            // Figure out if we need to pull from a child
            
            // get my value
            uint32_t mine = levels[lvl][idx].peek();
            
            // get left and right values
            uint32_t clvl = lvl+1;
            uint32_t lidx = 2*idx;
            uint32_t ridx = lidx+1;
            uint32_t lv = levels[clvl][lidx].peek();
            uint32_t rv = levels[clvl][ridx].peek();
            
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
            
            // swap the lists of parent and child
            mound_list_t * temp = levels[clvl][widx].list;
            levels[clvl][widx].list = levels[lvl][idx].list;
            levels[lvl][idx].list = temp;
            
            // move down a level
            lvl = clvl;
            idx = widx;
        }
    }
    
    /**
     * Extend an extra level for the mound.
     */
    __attribute__((noinline))
    void grow()
    {
#ifdef DIAGNOSTIC_MESSAGES
      //  analyze();
#endif
        
        // [mfs] TODO: add SSE
        uint32_t size = 1 << (bottom + 1);
        // NB:  calloc and malloc have roughly the same performance...
        // mound_cgl_node_t* newlevel = (mound_cgl_node_t*)calloc(size, sizeof(mound_cgl_node_t));
        mound_cgl_node_t* newlevel = (mound_cgl_node_t*)malloc(size * sizeof(mound_cgl_node_t));
        for (uint32_t i = 0; i < size; ++i)
            newlevel[i].list = NULL;
        
        // make the new level visible
        bottom++;
        levels[bottom] = newlevel;
    }
    
public:
    void analyze()
    {
        printf("Analysis: mound depth = %d\n", bottom);
        int total_elts = 0;
        for (uint32_t b = 0; b <= bottom; ++b) {
            int j = 0;
            for (int i = 0; i < (1 << b); i++) {
                mound_cgl_node_t * node = levels[b] + i;
                if (node->list != NULL)
                    j++;
            }
            printf("non-null treenodes at level %d = %d (expect %d) (%f percent)\n", b, j, (1<<b), ((float)j*100)/((float(1<<b))));
            total_elts += j;
        }
        printf("total non-null treenodes = %d (%f percent)\n", total_elts, ((float)total_elts*100)/((float)(1<<(bottom+1))-1));
        
        // now analyze lists at each level
        int counts[32];
        int overflow = 0;
        int total = 0;
        int total_lvl = 0;
        for (uint32_t b = 0; b <= bottom; ++b) {
            // clear the counts
            overflow = 0;
            total_lvl = 0;
            for (int i = 0; i < 32; ++i)
                counts[i] = 0;
            // count lists at this level
            for (int i = 0; i < (1<<b); i++) {
                int c = 0;
                mound_cgl_node_t* node = levels[b] + i;
                mound_list_t* tmp = node->list;
                while (tmp) {
                    c++;
                    tmp = tmp->next;
                }
                if (c < 32)
                    counts[c]++;
                else
                    overflow++;
                total_lvl += c;
                total += c;
            }
            // display results
            printf("List sizes: ");
            for (int i = 0; i < 32; ++i)
                printf("%d, ", counts[i]);
            printf("overflow = %d\n", overflow);
            printf("total elements at level = %d\n", total_lvl);
        }
        printf("total elements = %d\n", total);
    }
};

