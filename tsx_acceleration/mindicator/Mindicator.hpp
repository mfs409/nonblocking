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

#ifndef MINDICATOR_HPP__
#define MINDICATOR_HPP__

#include "list.hpp"
#include "skiplist.hpp"
#include "qc32.hpp"
#include "lin32.hpp"
#include "wf32.hpp"
#include "lin32_static.hpp"
#include "qc32_static.hpp"
#include "lockcache.hpp"
#include "lockmin.hpp"
#include "farray.hpp"
#include "common.hpp"
#include "mindicator_RTM.hpp"
#include "mindicator_RTM_fgl.hpp"
#include "mindicator_RTM_cgl.hpp"
#include "mindicator_cgl.hpp"

namespace mindicator
{
  /**
   * Mindicator data structure is a W-way tree with depth D.
   * Each leaf node is associated with a thread, and thread id (zero-based)
   * passed to arrive/depart function to determine the corresponding leaf.
   */
  template <int WAY, int DEPTH, class NODE>
  struct Mindicator
  {
      /***  Max number of threads supported by the sosi tree. */
      static const int NUM_NODES   = GeoSum<1, WAY, DEPTH>::value;
      static const int FIRST_LEAF  = GeoSum<1, WAY, DEPTH - 1>::value;

      /***  Constructor. */
      Mindicator()
      {
          for (int i = 0; i < NUM_NODES; i++) {
              nodes[i].word.fields.min = TOP;
              nodes[i].word.fields.word.bits.steady = STEADY;
              nodes[i].word.fields.word.bits.ver = 0;
              nodes[i].my_parent = get_parent(&nodes[i]);
              nodes[i].first_child = children(&nodes[i]);
              nodes[i].last_child = children(&nodes[i]) + WAY - 1;
          }
          nodes[0].my_parent = NULL;
      }

      /***  Get leaf node by index. */
      NODE* getnode(int index)
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
       *  Query root node of the Mindicator
       *
       *  [mfs] It might be interesting to try dynamically sizing the root.  We
       *        could do this by initially making the root the leftmost node at
       *        level K-1, and then pusing the root upward whenever more
       *        threads start using the tree.
       */
      int32_t query()
      {
          return nodes[0].word.fields.min;
      }

      /*** Indicate whether the specified node is leaf. */
      bool is_leaf(lin32_node_t* s)
      {
          int index = s - nodes;
          return FIRST_LEAF <= index && index < NUM_NODES;
      }

      /*** Get the parent of specified node. */
      NODE* get_parent(NODE* s)
      {
          int index = s - nodes;
          return &nodes[(index - 1) / WAY];
      }

      /*** Get the first child of specified node. */
      NODE* children(NODE* s)
      {
          int index = s - nodes;
          return &nodes[index * WAY + 1];
      }

    private:
      // [mfs] What would happen if threads 1 and 2 were distant, rather than
      //       adjacent?
      NODE nodes[NUM_NODES];
  };

  //typedef Mindicator<2, 7, qc32_node_t> mindicator_t;
  typedef sosillc_t mindicator_t;
}

#endif // MINDICATOR_HPP__

