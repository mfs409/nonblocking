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

/**
 *  XSOSI-R: SOSI implementation(Linearizable, Minimal Summary, Supports
 *  32-bit values)
 */

#ifndef MINDICATOR_LIN32_STATIC_HPP__
#define MINDICATOR_LIN32_STATIC_HPP__

#include "../common/platform.hpp"
#include "common.hpp"

namespace mindicator
{
  /**
   *  A 64-bit packed struct holding a value, a 31-bit counter, and a bool.
   *  Read with mvx if you need atomicity.
   */
  struct lin32s_node_t
  {
      // [mfs] does the order of these fields affect performance?
      volatile int32_t my_num;
      word64_t         word;
      lin32s_node_t*   my_parent;
      lin32s_node_t*   first_child;
      lin32s_node_t*   last_child;
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
          atomicswap32(&my_num, n);
#else
          my_num = n;
          WBR;
#endif
          // invoke arrive on parent
          arrive_internal(n);
      }

      /**
       *  Public interface for depart.  First depart from the appropriate
       *  per-thread leaf, and then propagate the departure up toward the
       *  root.
       */
      void depart()
      {
          // we will need a copy of the original value
          int32_t n = my_num;

          // write max at the leaf node... no CAS required, but we need
          // ordering
#ifdef STM_CPU_X86
          atomicswap32(&my_num, TOP);
#else
          my_num = TOP;
          WBR;
#endif
          // update the parent
          depart_internal(this, n);
      }

    private:

      /**
       *  This code propagates the arrival of value 'n' from a child of
       *  /this/ to /this/ node, and possibly recurses to push the arrival
       *  further upward.
       */
      void arrive_internal(int32_t n)
      {
          // The first step is to determine if our arrival with a value of
          // 'n', at a decendent of /this/, means that we must change the
          // value of this node.  In the ideal case, we don't need to
          // change the value, because this node has a value that is STEADY
          // and <= 'n'.  If that is the case, this loop will lead to us
          // returning immediately.  Note, however, that we must modify
          // /this/ by incrementing the version number, to avoid a race.
          word64_t x;

          while (true) {
              // atomically read the 64-bit versioned value of /this/
              mvx(&word.all, &x.all);

              // If the node value > n, or if the value is not steady, then
              // we can't take the quick exit
              if ((x.fields.min > n) || (!x.fields.word.bits.steady))
                  break;

              // We can take the quick exit... use a 32-bit CAS to
              // atomically increment the counter field
              word64_t temp;
              reinit_word(temp, x.fields.word.bits.steady, x.fields.min,
                          x.fields.word.bits.ver + 1);
#ifdef STM_CPU_SPARC
              if (bcas64(&word.all, x.all, temp.all))
                  return;
#else
              if (bcas32((uint32_t *)&word.all, (uint32_t)x.all,
                         (uint32_t)temp.all))
                  return;
#endif
              // [mfs] if we used cas64 instead of bcas64, then it would
              //       automatically reload the temp value for us...
          }

          // if n < this.word.val, then we need to use a CAS to update this
          // node so that its value == n, and so that it is TENTATIVE
          while (true) {
              // if we are not modifying the value, then we can exit this
              // loop
              if (n >= x.fields.min)
                  break;

              // special case for root
              if (!my_parent) {
                  word64_t temp;
                  reinit_word(temp, true, n, x.fields.word.bits.ver + 1);
                  if (bcas64(&word.all, x.all, temp.all)) {
                      return;
                  }
              }
              else {
                  word64_t temp;
                  reinit_word(temp, false, n, x.fields.word.bits.ver + 1);
                  if (bcas64(&word.all, x.all, temp.all)) {
                      x.all = temp.all;
                      break;
                  }
              }
              // reread word
              mvx(&word.all, &x.all);

          }

          // In the common case, the node is TENTATIVE, indicating that
          // some arriver (maybe not me) has updated this node.  We need to
          // propagate the value of this node upward, so that we are either
          // (a) propagating our own value up, or (b) propagating a
          // concurrent arriver up.  If we don't do this, then a future
          // query by this thread will violate processor consistency, by
          // appearing to happen before this arrive().
          if (!x.fields.word.bits.steady) {
              // first, we recurse upward to arrive at the parent
              //
              // [mfs] The current way of telling if we are working on the
              // root is by checking if my_parent is NULL.  That may not be
              // best in the long run.
              if (my_parent) {
                  // [mfs] Would rewriting to avoid recursion help?
                  my_parent->arrive_internal(n);
              }

              // Once we have successfully propagated upward, we can clear
              // the tentative mark from this node, and then return, which
              // will allow us to clear the tentative mark of descendents.
              //
              // [mfs] It seems that we only clear the tentative mark from
              //       a node if its value is the one that we are putting
              //       into the Mindicator.  Otherwise, the (presumably
              //       delayed) concurrent writer will need to clear that
              //       flag later.  Is this going to create pathologies,
              //       where we must propagate actions up the tree without
              //       actually doing modications to values, only because
              //       there is a concurrent TENTATIVE action that is
              //       delayed?
              if (x.fields.min == n) {
                  // [mfs] I really don't like it that we say 'n' here,
                  //       instead of x.fields.min.  I know they are equal,
                  //       but every time I see it I think there is a bug.
                  word64_t temp;
                  reinit_word(temp, true, n, x.fields.word.bits.ver + 1);
                  // [mfs] it is interesting to note that we do not need a
                  //       'while' loop around this CAS.  Since the x86 and
                  //       SPARC guarantee progress for a CAS, we know that
                  //       a failure must mean that the version number has
                  //       changed, in which case we are competing with
                  //       another concurrent operation, and that we can
                  //       leave without modifying this node.
                  //
                  // [mfs] With that said, there are two optimizations to
                  //       consider here.  First, we might want to test
                  //       before the CAS, so that we can avoid the
                  //       operation if it is certain to fail.
                  //
                  //       Second, it would be GREAT if we could avoid
                  //       doing 2 CASes on the root node.  At the entry to
                  //       this function, we could special-case it for the
                  //       root node, in order to only do one CAS.
#ifdef STM_CPU_SPARC
                  bcas64(&word.all, x.all, temp.all);
#else
                  bcas32((uint32_t*)&word.all, (uint32_t)x.all,
                         (uint32_t)temp.all);
#endif
              }
          }
      }

      /**
       *  This is the SOSI depart code for propagating a change upward
       */
      void depart_internal(lin32s_node_t* first, int32_t n)
      {
          lin32s_node_t* curr = first;

          while (true) {
              // compute the min value of children
              word64_t x;
              x.all = revisit(curr);

              // If the departure being propagated (as defined by the value
              // 'n') did not cause the value of this node to change (i.e.,
              // the min child is not departing), then we are done unless
              // there is a concurrent arrive that has made this node
              // TENTATIVE.  If there is a concurrent arrive, then it could
              // be masking the fact that we must propagate our departure
              // upward, and thus we must propagate anyway, or else future
              // queries may not linearize.
              if ((x.fields.min < n) && (x.fields.word.bits.steady))
                  return;

              // Propagate the depart up to the parent
              //
              // [mfs] The current way of telling if a node is root is by
              // looking at whether its got a NULL parent.  That may not be
              // best in the long run.
              if (!curr->my_parent)
                  return;

              curr = curr->my_parent;
          }
      }

      /**
       * Re-compute the min value of children, return true if the value
       * is changed.
       */
      uint64_t revisit(lin32s_node_t* curr)
      {
          while (true) {
              // the word is volatile... get a safe copy of it via 64-bit
              // atomic load
              word64_t x;
              mvx(&curr->word.all, &x.all);

              // if the node is tentative, it means one of my peers is
              // propagating an arrive up the chain.  By returning right
              // here, we'll climb up, which ensures that we will see a
              // steady node.
              //
              // [mfs] I am not certain about this logic, but I think that
              //       since we are departing, and thus putting _TOP into
              //       our own leaf, the tentative state indicates that
              //       this node is already updated appropriately (either
              //       the arriving number is < our old value, or else the
              //       state flipped to TENTATIVE after we wrote _TOP into
              //       our leaf).  The need to return and propagate upward
              //       stems from the fact that the arrive may be hiding
              //       the fact that we need to propagate our departure
              //       upward (e.g., if this node's parent still thinks
              //       that I am the min; that's the 'state flipped' clause
              //       from above), or else we lose linearizability
              if (!x.fields.word.bits.steady)
                  return x.all;

              // compute mvc: min value of children
              //
              // NB: we don't need to do atomic 64-bit reads if we are only
              //     working with an aligned 32-bit field within the packed
              //     struct
              int32_t mvc = curr->my_num;  // min value of all children
              lin32s_node_t* begin = curr->first_child;
              lin32s_node_t* end = curr->last_child;
              if (begin != NULL) {
                  for (lin32s_node_t* n = begin; n <= end; n++) {
                      int32_t lmin = n->word.fields.min;
                      if (mvc > lmin)
                          mvc = lmin;
                  }
              }

              // if the minimum value over all children is less than the
              // current value of this node, then this is an intermediate
              // node, and there is an arriver in-flight.  We need to help
              // the arriver, who is tentative.
              //
              // [mfs] since x.fields.state != TENTATIVE, I think we know a
              //       little bit more.  It must be the case that the
              //       arriver is in the process of trying to modify this
              //       node, as it must have already modified one of the
              //       children, and so must now be working on this.  That
              //       being the case, it might make more sense for us to
              //       spin briefly, then wait to see if word.all has
              //       changed from x.all, because we really don't want to
              //       do a CAS if we don't have to, and if arrive
              //       operations are faster (they may be, especially for
              //       large W, since this operation is touching all W
              //       children), then in the common case, we are going to
              //       fail on our CAS right here.
              if (mvc < x.fields.min) {
                  // [mfs] I'm guessing that what this means is that we are
                  //       propagating up an in-flight arriver, which means
                  //       that we have to put the arriver's value in this
                  //       node and mark it as tentative so that the arrive
                  //       will proceed properly.  Of course, if the
                  //       arriver is truly delayed, then we are probably
                  //       going to end up propagating the arrive as far up
                  //       as it will go.
                  //
                  // [mfs] Is there a pathology here?  If I propagate the
                  //       arrive all the way up, and I leave this node as
                  //       tentative, then have I just committed all future
                  //       operations to take a slow path, until the
                  //       tentative arriver awakes and finishes?
                  word64_t temp;
                  reinit_word(temp, false, mvc, x.fields.word.bits.ver + 1);
                  // [mfs] bcas64 is expensive... perhaps we should verify
                  //       that word.all == x.all before trying?
                  if (bcas64(&curr->word.all, x.all, temp.all))
                      return temp.all;
              }
              // the minimum value over all children is >= the current
              // value of the node.  The original comment was that we need
              // to "lift it up"
              //
              // [mfs] I think this means that the departing thread was the
              //       previous min.  In that case, we are changing this
              //       value by increasing it, which means that a parent
              //       may have a copy of this value and need to be
              //       updated, too.  The '=' condition is annoying, as it
              //       would seem that in the case of '=', no CAS would be
              //       needed, but I think yjl has a case in which failing
              //       to CAS on '=' (and hence failing to bump the version
              //       number) leads to ABA problems.
              else {
                  // [mfs] again, we might want to check word.all first...
                  word64_t temp;
                  reinit_word(temp, true, mvc, x.fields.word.bits.ver + 1);
                  if (bcas64(&curr->word.all, x.all, temp.all))
                      return temp.all;
              }
          }
      }
  };

  /**
   * SOSI data structure is a W-way tree with depth D.
   * Each leaf node is associated with a thread, and thread id (zero-based)
   * passed to arrive/depart function to determine the corresponding leaf.
   */
  template <int WAY, int DEPTH>
  struct xsosir64_t
  {
      static const int NUM_NODES   = GeoSum<1, WAY, DEPTH>::value;
      static const int FIRST_LEAF  = GeoSum<1, WAY, DEPTH - 1>::value;

      /**
       *  Constructor.
       */
      xsosir64_t()
      {
          for (int i = 0; i < NUM_NODES; i++) {
              nodes[i].my_num = TOP;
              nodes[i].word.fields.min = TOP;
              nodes[i].word.fields.word.bits.steady = true;
              nodes[i].word.fields.word.bits.ver = 0;
              nodes[i].my_parent = get_parent(&nodes[i]);
              nodes[i].first_child = children(&nodes[i]);
              nodes[i].last_child = children(&nodes[i]) + WAY - 1;
          }
          nodes[0].my_parent = NULL;
          for (int i = FIRST_LEAF; i < NUM_NODES; i++)
              nodes[i].first_child = NULL;
      }

      /***  Get leaf node by index... was operator[] */
      lin32s_node_t* getnode(int index)
      {
          return &nodes[index];
      }

      /**
       *  Query root node of sosi tree.
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

      /*** Get the parent of specified node. */
      lin32s_node_t* get_parent(lin32s_node_t* s)
      {
          int index = s - nodes;
          return &nodes[(index - 1) / WAY];
      }

      /*** Get the first child of specified node. */
      lin32s_node_t* children(lin32s_node_t* s)
      {
          int index = s - nodes;
          return &nodes[index * WAY + 1];
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

    private:
      // [mfs] What would happen if threads 1 and 2 were distant, rather than
      //       adjacent?
      lin32s_node_t nodes[NUM_NODES];
  };

} // namespace mindicator

#endif // MINDICATOR_LIN32_STATIC_HPP__
