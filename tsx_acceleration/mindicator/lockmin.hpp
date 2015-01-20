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


#ifndef SOSILMINIM_HPP
#define SOSILMINIM_HPP

#include "../common/platform.hpp"
#include "../common/locks.hpp"
#include "common.hpp"

namespace mindicator
{

/** Forward declaration of sosi tree. */
template<int W, int D>
class sosilminim_t;

/**
 * Represent a sosi node.
 */
template<int W, int D>
class sosilminim_node_t {

    /** Give access to sosilminim_t class. */
    friend class sosilminim_t<W, D>;

  public:
    /**
     * Arrive at sosi node.
     */
    void arrive(int32_t n);

    /**
     * Depart from sosi node.
     */
    void depart();

  private:
    sosilminim_t<W, D> *    tree;  // pointer to tree
    volatile uintptr_t      lock;  // lock of the node
    volatile int32_t        value; // timestamp value
};

/**
 * Each leaf node is associated with a thread, and thread id (zero-based)
 * passed to arrive/depart function to determine the corresponding leaf.
 */
template<int W, int D>
class sosilminim_t
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
    sosilminim_t()
    {
        for (int i = 0; i < NUM_NODES; i++) {
            nodes[i].tree = this;
            nodes[i].lock = 0;
            nodes[i].value = INT_MAX;
        }
    }

    /**
     * Get leaf node by index.
     */
    sosilminim_node_t<W, D>* getnode(int index)
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
        return nodes[0].value;
    }

  public:

    /**
     * Indicate whether the specified node is root.
     */
    bool is_root(sosilminim_node_t<W, D> *s)
    {
        return nodes == s;
    }

    /**
     * Indicate whether the specified node is leaf.
     */
    bool is_leaf(sosilminim_node_t<W, D> *s)
    {
        int index = s - nodes;
        return FIRST_LEAF <= index && index < NUM_NODES;
    }

    /**
     * Get the parent of specified node.
     */
    sosilminim_node_t<W, D> * parent(sosilminim_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[(index - 1) / WAY];
    }

    /**
     * Get the first child of specified node.
     */
    sosilminim_node_t<W, D> * children(sosilminim_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[index * WAY + 1];
    }

  private:
    sosilminim_node_t<W, D> nodes[NUM_NODES];
};

template<int W, int D>
void sosilminim_node_t<W, D>::arrive(int32_t n)
{
    // lock this node
    tatas_acquire(&lock);

    // indicate whether the parent is locked
    bool pLock = false;

    // if n > value, stop propagation
    if (n < value) {
        // arrive at parent first
        if (!tree->is_root(this)) {
            tree->parent(this)->arrive(n);
            pLock = true;
        }

        // set value to n
        value = n;
    }

    // unlock parent
    if (pLock)
        tatas_release(&tree->parent(this)->lock);

    // unlock if arrive regress to last node
    if (tree->is_leaf(this))
        tatas_release(&lock);
}

template<int W, int D>
void sosilminim_node_t<W, D>::depart()
{
    // lock this node
    tatas_acquire(&lock);

    // compute mvc: min value of children
    int32_t mvc;
    if (tree->is_leaf(this)) {
        mvc = INT_MAX;
    }
    else {
        sosilminim_node_t<W, D> * begin = tree->children(this);
        sosilminim_node_t<W, D> * end = begin + tree->WAY;
        mvc = begin->value;
        for (sosilminim_node_t<W, D> * n = begin + 1; n < end; n++) {
            int32_t temp = n->value;
            if (mvc > temp)
                mvc = temp;
        }
    }

    // skip if value = mvc(value cannot be > mvc)
    if (value < mvc) {
        // set value to mvc
        value = mvc;

        // depart parent
        if (!tree->is_root(this))
            tree->parent(this)->depart();
    }

    // unlock this node
    tatas_release(&lock);
}
} // namespace mindicator

#endif
