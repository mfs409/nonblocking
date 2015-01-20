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

/*** Quiesciently Consistent Mindicator capable of holding 32-bit values */

#ifndef MINDICATOR_QC32_HPP__
#define MINDICATOR_QC32_HPP__

#include <cassert>
#include "../common/platform.hpp"
#include "common.hpp"

namespace mindicator
{
  /*** Represent a sosi node. */
  struct qc32_node_t
  {
      // [mfs] does the order of these fields affect performance?
      word64_t          word;  // per-node data
      qc32_node_t*      my_parent;
      qc32_node_t*      first_child;
      qc32_node_t*      last_child;
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
          word64_t temp;
          MAKE_WORD(temp, STEADY, n, 0);
#ifdef STM_CPU_X86
          atomicswap64(&word.all, temp.all);
#else
          word.all = temp.all;
          WBR;
#endif
          // invoke arrive on parent
          my_parent->arrive_internal(n);
      }

      /**
       *  Public interface for depart.  First depart from the appropriate
       *  per-thread leaf, and then propagate the departure up toward the root.
       */
      void depart()
      {
          // we will need a copy of the original value
          int32_t n = word.fields.min;

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
          depart_internal(my_parent, n);
      }

    private:

      /**
       *  This code propagates the arrival of value 'n' from a child of /this/
       *  to /this/ node, and possibly recurses to push the arrival further
       *  upward.
       */
      void arrive_internal(int32_t n)
      {
          // The first step is to determine if our arrival with a value of 'n',
          // at a decendent of /this/, means that we must change the value of
          // this node.  In the ideal case, we don't need to change the value,
          // because this node has a value that is STEADY and <= 'n'.  If that
          // is the case, this loop will lead to us returning immediately.
          // Note, however, that we must modify /this/ by incrementing the
          // version number, to avoid a race.
          word64_t x;

          while (true) {

              // atomically read the 64-bit versioned value of /this/
              read_word(&word, &x);

              // Need to decrease min, then propagate to ancestors
              if (x.fields.min > n) {
                  word64_t temp;
                  MAKE_WORD(temp, TENTATIVE, n, x.fields.word.bits.ver + 1);
                  if (bcas64(&word.all, x.all, temp.all)) {
                      // Call arrive on parent
                      if (my_parent)
                          my_parent->arrive_internal(n);
                      // All ancestors now <= n, so we can set steady bit
                      word64_t temp2;
                      MAKE_WORD(temp2, STEADY, n, x.fields.word.bits.ver + 2);
                      bcas64(&word.all, temp.all, temp2.all);
                      return;
                  }
              }
              // No local modification needed, but must propagate to ancestors
              else if (x.fields.word.bits.steady == TENTATIVE) {
                  // first, we recurse upward to arrive at the parent
                  if (my_parent)
                      my_parent->arrive_internal(n);
                  // Once we have successfully propagated upward, we can clear the
                  // tentative mark from this node, and then return, which will
                  // allow us to clear the tentative mark of descendents.
                  //
                  if (x.fields.min == n) {
                      word64_t temp;
                      MAKE_WORD(temp, STEADY, n, x.fields.word.bits.ver + 1);
#ifdef STM_CPU_SPARC
                      bcas64(&word.all, x.all, temp.all);
#else
                      bcas32((uint32_t*)&word.all, (uint32_t)x.all, (uint32_t)temp.all);
#endif
                  }
                  return;
              }
              else {
                  // We can take the quick exit... use a 32-bit CAS to atomically
                  // increment the counter field
                  word64_t temp;
                  MAKE_WORD(temp, x.fields.word.bits.steady, x.fields.min, x.fields.word.bits.ver + 1);
#ifdef STM_CPU_SPARC
                  if (bcas64(&word.all, x.all, temp.all))
                      return;
#else
                  if (bcas32((uint32_t *)&word.all, (uint32_t)x.all, (uint32_t)temp.all))
                      return;
#endif
              }
          }
      }

      /***  This is the SOSI depart code for propagating a change upward */
      void depart_internal(qc32_node_t* first, int32_t n)
      {
          qc32_node_t* curr = first;

          while (true) {
              // return if revisit indicates
              if (revisit(curr, n))
                  return;

              // Propagate the depart up to the parent
              if (!curr->my_parent)
                  return;

              curr = curr->my_parent;
          }
      }

      /**
       * Re-compute the min value of children, return true if the value
       * is changed.
       */
      bool revisit(qc32_node_t* curr, int32_t n)
      {
          while (true) {
              // the word is volatile... get a safe copy of it via 64-bit
              // atomic load
              word64_t x;
              read_word(&curr->word, &x);

              // [lyj] should be okay to stop traversal if x = n and x is dirty
              if (x.fields.word.bits.steady == TENTATIVE)
                  return (x.fields.min <= n);

#if 0
              // [opt] if the old value > n, we don't need to recompute the minimum,
              // because this level has been cleaned by a helper. But we still have
              // to recurse to the parent to continue clean up.
              if (x.fields.min > n)
                  return false;

              // [opt] if the old value is < n, we can simply performs a counter cas
              // (and finished the whole depart if succeeds), instead of the much
              // more expansive all-children scan.
              if (x.fields.min < n) {
                  word64_t t1;
                  MAKE_WORD(t1, STEADY, x.fields.min, x.fields.word.bits.ver + 1);
                  if (bcas64(&curr->word.all, x.all, t1.all))
                      return true;
                  continue;
              }
#endif

              // compute mvc: min value of children
              //
              // NB: we don't need to do atomic 64-bit reads if we are only
              //     working with an aligned 32-bit field within the packed
              //     struct
              qc32_node_t* begin = curr->first_child;
              qc32_node_t* end = curr->last_child;
              int32_t mvc = begin->word.fields.min;  // min val of all children
              for (qc32_node_t* c = begin + 1; c <= end; c++) {
                  int32_t lmin = c->word.fields.min;
                  if (mvc > lmin)
                      mvc = lmin;
              }

              // if mvc <= n, mvc should be from another process q which will
              // clean up value n at each node on p's path
              if (mvc <= n)
                  return true;

              uint32_t aok = (mvc >= x.fields.min);
              word64_t temp;
              MAKE_WORD(temp, aok, mvc, x.fields.word.bits.ver + 1);
              if (bcas64(&curr->word.all, x.all, temp.all))
                  return (x.fields.min < n);  // this is always true with the above [opt]s
          }
      }
  };

} // namespace mindicator

#endif // MINDICATOR_QC32_HPP__
