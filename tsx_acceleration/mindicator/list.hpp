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


#ifndef SOSILLC_HPP
#define SOSILLC_HPP

#include "../common/platform.hpp"

/**
 * Represent a sosi node.
 */
struct sosillc_node_t {
    volatile int32_t          value; // timestamp value
    sosillc_node_t * volatile next;  // pointer to next node
    sosillc_node_t * volatile prev;  // pointer to next node
};

/**
 * SOSI implementation based on CGL linked list.
 */
class sosillc_t
{
  public:

    /**
     * Constructor.
     */
    sosillc_t()
    {
        head.next = &tail;
        head.prev = NULL;
        tail.prev = &head;
        tail.next = NULL;
        tail.value = INT_MAX;
        min = tail.value;
        lock = 0;
    }

    /**
     * Compatible for the thread indexed arrive/depart api.
     */
    sosillc_t * getnode(int)
    {
        return this;
    }

    /**
     * Query root node of sosi tree.
     */
    int32_t query()
    {
        return min;
    }

    /**
     * Arrive insert a node in the sorted linked list.
     */
    void arrive(int32_t n) __attribute__((noinline));

    /**
     * Arrive insert a node in the sorted linked list.
     */
    void arrive(int32_t, int32_t n)
    {
        arrive(n);
    }

    /**
     * Depart remove the node from the linked list.
     */
    void depart() __attribute__((noinline));

    /**
     * Depart remove the node from the linked list.
     */
    void depart(int32_t)
    {
        depart();
    }

  private:
    sosillc_node_t   head;
    sosillc_node_t   tail;
    uintptr_t        lock;
    volatile int32_t min;
};

#endif
