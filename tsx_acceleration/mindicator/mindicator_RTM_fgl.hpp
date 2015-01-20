///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007, 2008, 2009
// University of Rochester
// Department of Computer Science
// All rights reserved.
//
// Copyright (c) 2009, 2010
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


#ifndef SOSILRTM_FGL_HPP
#define SOSILRTM_FGL_HPP

#include "../common/platform.hpp"
#include "../common/locks.hpp"
#include "common.hpp"

namespace mindicator
{

/** Forward declaration of sosi tree. */
template<int W, int D>
class sosilRTM_fgl_t;

/**
 * Represent a sosi node.
 */
template<int W, int D>
class sosilRTM_fgl_node_t {

    /** Give access to sosilRTM_fgl_t class. */
    friend class sosilRTM_fgl_t<W, D>;

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
    sosilRTM_fgl_t<W, D> *    tree;  // pointer to tree
    volatile uintptr_t      lock;  // lock of the node
    volatile int32_t        value; // timestamp value
};

/**
 * Each leaf node is associated with a thread, and thread id (zero-based)
 * passed to arrive/depart function to determine the corresponding leaf.
 */
template<int W, int D>
class sosilRTM_fgl_t
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
    sosilRTM_fgl_t()
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
    sosilRTM_fgl_node_t<W, D>* getnode(int index)
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
    bool is_root(sosilRTM_fgl_node_t<W, D> *s)
    {
        return nodes == s;
    }

    /**
     * Indicate whether the specified node is leaf.
     */
    bool is_leaf(sosilRTM_fgl_node_t<W, D> *s)
    {
        int index = s - nodes;
        return FIRST_LEAF <= index && index < NUM_NODES;
    }

    /**
     * Get the parent of specified node.
     */
    sosilRTM_fgl_node_t<W, D> * parent(sosilRTM_fgl_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[(index - 1) / WAY];
    }

    /**
     * Get the first child of specified node.
     */
    sosilRTM_fgl_node_t<W, D> * children(sosilRTM_fgl_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[index * WAY + 1];
    }

  private:
    sosilRTM_fgl_node_t<W, D> nodes[NUM_NODES];
};

template<int W, int D>
void sosilRTM_fgl_node_t<W, D>::arrive(int32_t n)
{
    uint32_t status;
    status = _xbegin();
    if(status == _XBEGIN_STARTED)
    {
        if(lock != 0)
            _xabort(6);
        
        sosilRTM_fgl_node_t<W, D> *current = this;
        while(n < current->value)
        {
            current->value = n;
            if (!tree->is_root(current)) {
                 current = tree->parent(current);
                if(current->lock != 0)
                    _xabort(6);
            }else
                break;
        }
        _xend();
        
    }else
    {
        // lock this node
        tatas_acquire(&lock);
        sosilRTM_fgl_node_t<W, D> *current = this;
        while(n < current->value)
        {
            current->value = n;
            if (!tree->is_root(current)) {
                current = tree->parent(current);
                tatas_acquire(&current->lock);
            }else
                break;
        }
    
        sosilRTM_fgl_node_t<W, D> *back = this;
        // The sequence of releasing lock... ...
        /*while(current != this)
        {
            while (current != tree->parent(back)) {
                back = tree->parent(back);
            }
            tatas_release(&current->lock);
            current = back;
            back = this;
        }
        tatas_release(&lock);*/
    
        tatas_release(&current->lock);
        while(back != current)
        {
            tatas_release(&back->lock);
            back = tree->parent(back);
        }

    
    }
    
}

template<int W, int D>
void sosilRTM_fgl_node_t<W, D>::depart()
{
    uint32_t status;
    status = _xbegin();
    if(status == _XBEGIN_STARTED)
    {
        
        if(lock != 0)
            _xabort(6);
        
        sosilRTM_fgl_node_t<W, D> *current = this;
        int32_t mvc = INT_MAX;
        
        while(current->value < mvc)
        {
            current->value = mvc;
            if (!tree->is_root(current)) {
                current = tree->parent(current);
                if(current->lock != 0)
                    _xabort(6);
            }else
                break;
            sosilRTM_fgl_node_t<W, D> * begin = tree->children(current);
            sosilRTM_fgl_node_t<W, D> * end = begin + tree->WAY;
            mvc = begin->value;
            for (sosilRTM_fgl_node_t<W, D> * n = begin + 1; n < end; n++) {
                int32_t temp = n->value;
                if (mvc > temp)
                    mvc = temp;
            }
        }
        _xend();
    }else
    {
        // lock this node
        tatas_acquire(&lock);
        
        sosilRTM_fgl_node_t<W, D> *current = this;
        int32_t mvc = INT_MAX;
        
        while(current->value < mvc)
        {
            current->value = mvc;
            if (!tree->is_root(current)) {
                current = tree->parent(current);
                tatas_acquire(&current->lock);
            }else
                break;
            sosilRTM_fgl_node_t<W, D> * begin = tree->children(current);
            sosilRTM_fgl_node_t<W, D> * end = begin + tree->WAY;
            mvc = begin->value;
            for (sosilRTM_fgl_node_t<W, D> * n = begin + 1; n < end; n++) {
                int32_t temp = n->value;
                if (mvc > temp)
                    mvc = temp;
            }
        }
        sosilRTM_fgl_node_t<W, D> *back = this;
        
        // The sequence of releasing lock... ...
        /*while(current != this)
        {
            while (current != tree->parent(back)) {
                back = tree->parent(back);
            }
            tatas_release(&current->lock);
            current = back;
            back = this;
        }
        tatas_release(&lock);*/
        tatas_release(&current->lock);
        while(back != current)
        {
            tatas_release(&back->lock);
            back = tree->parent(back);
        }
    }
    
    
}
} // namespace mindicator

#endif

