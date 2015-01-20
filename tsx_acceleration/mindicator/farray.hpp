///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2010
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

/*** f-Array Mindicator */

#ifndef FARRAY_HPP__
#define FARRAY_HPP__

#include "../common/platform.hpp"
#include "common.hpp"

namespace mindicator
{
  /*** Represent a sosi node. */
  struct farray_node_t
  {
      // [mfs] does the order of these fields affect performance?
      word64_t          word;  // per-node data
      farray_node_t*     my_parent;
      farray_node_t*     first_child;
      farray_node_t*     last_child;
      char pad[64 - sizeof(word64_t) - 3 * sizeof(void*)];

      /**
       *  Public interface for arrive.  We arrive at our leaf node, and then we
       *  propagate the arrival upward
       */
      void arrive(int32_t n)
      {
          // Write number at the leaf node.  We need WBR ordering, but we can
          // skip making this TENTATIVE, since nobody ever reads the TENTATIVE
          // bit of leaves
#ifdef STM_CPU_X86
          word64_t temp;
          MAKE_WORD(temp, 0, n, 0);
          atomicswap64(&word.all, temp.all);
#else
          word.fields.min = n;
          WBR;
#endif
          // invoke arrive on parent
          propagate(my_parent);
      }

      /**
       *  Public interface for depart.  First depart from the appropriate
       *  per-thread leaf, and then propagate the departure up toward the root.
       */
      void depart()
      {
          // write max at the leaf node... no CAS required, but we need
          // ordering
#ifdef STM_CPU_X86
          word64_t temp;
          MAKE_WORD(temp, STEADY, TOP, 0);
          atomicswap64(&word.all, temp.all);
#else
          word.fields.min = TOP;
          WBR;
#endif
          // update the parent
          propagate(my_parent);
      }

    private:
      /**
       *  This code propagates the arrival of value 'n' from a child of /this/
       *  to /this/ node, and possibly recurses to push the arrival further
       *  upward.
       */
      void propagate(farray_node_t * first)
      {
          farray_node_t* curr = first;

          while (true) {
              // compute the min value of children
              if (!revisit(curr))
                  revisit(curr);
              if (!curr->my_parent)
                  return;
              curr = curr->my_parent;
          }
      }

      /**
       * Re-compute the min value of children, return true if the value
       * is changed.
       */
      bool revisit(farray_node_t* curr)
      {
          // the word is volatile... get a safe copy of it via 64-bit
          // atomic load
          word64_t x;
          read_word(&curr->word, &x);

          // compute mvc: min value of children
          //
          // NB: we don't need to do atomic 64-bit reads if we are only
          //     working with an aligned 32-bit field within the packed
          //     struct
          farray_node_t* begin = curr->first_child;
          farray_node_t* end = curr->last_child;
          int32_t mvc = begin->word.fields.min;  // min val of all children
          for (farray_node_t* c = begin + 1; c <= end; c++) {
              int32_t lmin = c->word.fields.min;
              if (mvc > lmin)
                  mvc = lmin;
          }

          word64_t temp;
          MAKE_WORD(temp, 0, mvc, x.fields.word.bits.ver + 1);
          return (bcas64(&curr->word.all, x.all, temp.all));
      }


  };

} // namespace mindicator

#endif // FARRAY_HPP__
