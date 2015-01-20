///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007, 2008, 2009
// University of Rochester
// Department of Computer Science
// All rights reserved.
//
// Copyright (c) 2009, 2010
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


#ifndef SOSILCACHE_HPP
#define SOSILCACHE_HPP

/**
 *  SOSI-L-CACHE: SOSI implementation that uses fine-grained locking and caches
 *  all childrens' values in the parent.
 */

#include "../common/platform.hpp"
#include "../common/locks.hpp"
#include "common.hpp"

namespace mindicator
{

/** Forward declaration of sosi tree. */
template<int W, int D>
class sosilcache_t;

/**
 * Represent a sosi node.
 */
template<int W, int D>
class sosilcache_node_t {

    /** Give access to sosilcache_t class. */
    friend class sosilcache_t<W, D>;

  public:
    /**
     * Arrive at sosi node.
     */
    void arrive(int32_t n)
    {
        arrive(n, 0);
    }

    /**
     * Depart from sosi node.
     */
    void depart()
    {
        depart(INT_MAX, 0);
    }

  private:

    /**
     * Internal arrive function.
     */
    inline void arrive(int32_t n, int32_t rIndex);

    /**
     * Internal depart function.
     */
    inline void depart(int32_t n, int32_t rIndex);

  private:
    sosilcache_t<W, D> *    tree;     // pointer to tree
    volatile uintptr_t      lock;     // lock of the node
    volatile int32_t        cache[W]; // cache of children
    volatile int32_t        min;      // min of cache
};

/**
 * Each leaf node is associated with a thread, and thread id (zero-based)
 * passed to arrive/depart function to determine the corresponding leaf.
 */
template<int W, int D>
class sosilcache_t
{
  public:

    /**
     * Max number of threads supported by the sosi tree.
     */
    static const int WAY = W;
    static const int DEPTH = D;
    static const int MAX_THREADS = Power<WAY, DEPTH - 1>::value;
    static const int NUM_NODES = GeoSum<1, WAY, DEPTH>::value;
    static const int FIRST_LEAF = GeoSum<1, WAY, DEPTH - 1>::value;

  public:

    /**
     * Constructor.
     */
    sosilcache_t()
    {
        for (int i = 0; i < NUM_NODES; i++) {
            nodes[i].tree = this;
            nodes[i].lock = 0;
            nodes[i].min = INT_MAX;
            for (int j = 0; j < W; j++)
                nodes[i].cache[j] = INT_MAX;
        }
    }

    /**
     * Get leaf node by index.
     */
    sosilcache_node_t<W, D>* getnode(int index)
    {
        return &nodes[FIRST_LEAF + index];
    }

    /*** new interface: Arrive at the Mindicator, not at a node */
    void arrive(int index, int32_t n)
    {
        getnode(index)->arrive(n);
    }

    /*** new interface: Depart at the Mindicator, not at a node */
    void depart(int index)
    {
        getnode(index)->depart();
    }

    /**
     * Query root node of sosi tree.
     */
    int32_t query()
    {
        return nodes[0].min;
    }

  public:

    /**
     * Indicate whether the specified node is root.
     */
    bool is_root(sosilcache_node_t<W, D> *s)
    {
        return nodes == s;
    }

    /**
     * Indicate whether the specified node is leaf.
     */
    bool is_leaf(sosilcache_node_t<W, D> *s)
    {
        int index = s - nodes;
        return FIRST_LEAF <= index && index < NUM_NODES;
    }

    /**
     * Get the parent of specified node.
     */
    sosilcache_node_t<W, D> * parent(sosilcache_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[(index - 1) / WAY];
    }

    /**
     * Get the first child of specified node.
     */
    sosilcache_node_t<W, D> * children(sosilcache_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[index * WAY + 1];
    }

    int32_t relative_index(sosilcache_node_t<W, D> *s)
    {
        int index = s - nodes;
        return (index - 1) % WAY;
    }

  private:
    sosilcache_node_t<W, D> nodes[NUM_NODES];
};

template<int W, int D>
void sosilcache_node_t<W, D>::arrive(int32_t n, int32_t rIndex)
{
    // lock this node
    tatas_acquire(&lock);

    // indicate whether the parent is locked
    bool pLock = false;

    // if n >= min, stop propagation
    if (n < min) {
        // arrive at parent first
        if (!tree->is_root(this)) {
            tree->parent(this)->arrive(n, tree->relative_index(this));
            pLock = true;
        }

        // set new min value of cache
        min = n;
    }

    // set cache slot to n
    CFENCE;
    cache[rIndex] = n;

    // unlock parent
    CFENCE;
    if (pLock)
        tatas_release(&tree->parent(this)->lock);

    // unlock if arrive regress to last node
    if (tree->is_leaf(this))
        tatas_release(&lock);
}

template<int W, int D>
void sosilcache_node_t<W, D>::depart(int32_t n, int32_t rIndex)
{
    // lock this node
    tatas_acquire(&lock);

    // save old cache value
    int32_t oldCache = cache[rIndex];

    // update cache
    cache[rIndex] = n;

    // skip if this is not min slot
    if (oldCache == min) {
        // update cache
        cache[rIndex] = n;

        // re-compute the min value of cache
        int32_t temp = cache[0];
        for (int i = 1; i < W; i++)
            if (temp > cache[i])
                temp = cache[i];
        min = temp;

        // depart parent
        if (!tree->is_root(this))
            tree->parent(this)->depart(min, tree->relative_index(this));
    }

    // unlock this node
    tatas_release(&lock);
}

} // namespace mindicator

#endif
