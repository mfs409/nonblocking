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

/**
 *  SOSI-R-MINIM: SOSI implementation(Linearizable, Minimal Summary, Supports
 *  32-bit values)
 */

#ifndef SOSIWMINIM64_HPP
#define SOSIWMINIM64_HPP

#include "../common/platform.hpp"
#include "common.hpp"

namespace mindicator
{

/**
 *  Helper macro for using the sosiwminim64_word_t
 */
#define MAKE_SOSIWMINIM64_WORD(_var, _s, _d, _v)                  \
    { _var.fields.state = _s; _var.fields.min = _d; _var.fields.ver = _v; }

/**
 *  Forward declaration of sosi tree.
 *  SOSI structure is a W-way tree with depth D.
 */
template <int W, int D>
struct sosiwminim64_t;

/**
 *  Represent a sosi node.
 *
 *  [mfs] I'm a little rusty on my C++ rules... can this be a subclass of
 *        sosiwminim64_t?  It would eliminate some friend and public issues
 */
template <int W, int D>
class sosiwminim64_node_t
{
    friend class sosiwminim64_t<W, D>;

  public:

    /**
     *  Track the different states that an intermediate node can be in
     */
    enum sosiwminim64_node_state_t {
        TENTATIVE = 0,  // this node has an arrive() operation that has not
                        // come back down the tree yet
        STEADY    = 1   // no outstanding operations on this node?
    };

    /**
     *  A 64-bit packed struct holding a value, a 31-bit counter, and a bool.
     *  Read with mvx if you need atomicity.
     */
    union sosiwminim64_word_t {
        volatile struct {
            uint32_t state : 1;   // state {tentative, steady}
            uint32_t ver   : 31;  // version number
            int32_t  min   : 32;  // cache of min value of children
        } fields;
        volatile uint64_t all;
    } __attribute__ ((aligned(8)));

    /** Max number supported. */
    static const int32_t MAX = INT_MAX;

    /**
     * Arrive at sosi node.
     */
    void arrive(int32_t n);

    /**
     * Depart from sosi node.
     */
    void depart();

  private:

    /**
     * Arrive at sosi node.
     */
    void arrive_internal(int32_t n);

    /**
     * Depart from sosi node.
     */
    void depart_internal(sosiwminim64_node_t<W, D> * first, int32_t n);

    /**
     * Re-compute the min value of children.
     */
    uint64_t revisit(sosiwminim64_node_t<W, D> * curr);

  private:
    // [mfs] does the order of these fields affect performance?
    sosiwminim64_t<W, D>*        tree;  // pointer to tree
    sosiwminim64_word_t          word;  // per-node data
    // [mfs] these are not padded to a 64-byte cache line... that's probably
    //       not good for our scalability
};


/**
 * SOSI data structure is a W-way tree with depth D.
 * Each leaf node is associated with a thread, and thread id (zero-based)
 * passed to arrive/depart function to determine the corresponding leaf.
 */
template <int W, int D>
struct sosiwminim64_t
{
    /**
     *  Max number of threads supported by the sosi tree.
     */
    static const int WAY         = W;
    static const int DEPTH       = D;
    static const int MAX_THREADS = Power<WAY, DEPTH - 1>::value;
    static const int NUM_NODES   = GeoSum<1, WAY, DEPTH>::value;
    static const int FIRST_LEAF  = GeoSum<1, WAY, DEPTH - 1>::value;

    /**
     *  Constructor.
     */
    sosiwminim64_t()
    {
        for (int i = 0; i < NUM_NODES; i++) {
            nodes[i].tree = this;
            nodes[i].word.fields.min = sosiwminim64_node_t<W, D>::MAX;
            nodes[i].word.fields.state = sosiwminim64_node_t<W, D>::STEADY;
            nodes[i].word.fields.ver = 0;
        }
    }

    /**
     *  Get leaf node by index.
     */
    sosiwminim64_node_t<W, D>* getnode(int index)
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
     *  Query root node of sosi tree.
     */
    int32_t query()
    {
        return nodes[0].word.fields.min;
    }

    /**
     * Indicate whether the specified node is root.
     */
    bool is_root(sosiwminim64_node_t<W, D> *s)
    {
        return nodes == s;
    }

    /**
     * Indicate whether the specified node is leaf.
     */
    bool is_leaf(sosiwminim64_node_t<W, D> *s)
    {
        int index = s - nodes;
        return FIRST_LEAF <= index && index < NUM_NODES;
    }

    /**
     * Get the parent of specified node.
     */
    sosiwminim64_node_t<W, D> * parent(sosiwminim64_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[(index - 1) / WAY];
    }

    /**
     * Get the first child of specified node.
     */
    sosiwminim64_node_t<W, D> * children(sosiwminim64_node_t<W, D> *s)
    {
        int index = s - nodes;
        return &nodes[index * WAY + 1];
    }

  private:
    sosiwminim64_node_t<W, D> nodes[NUM_NODES];
};

/**
 *  Public interface for arrive.  We arrive at our leaf node, and then we
 *  propagate the arrival upward
 */
template <int W, int D>
void sosiwminim64_node_t<W, D>::arrive(int32_t n)
{
    sosiwminim64_word_t temp;

    // write a tentative number at the leaf node
    MAKE_SOSIWMINIM64_WORD(temp, TENTATIVE, n, 0);
    word.all = temp.all;
    WBR;

    // invoke arrive on parent
    //
    // [mfs] there is a lot of redundancy in this code... every time we arrive,
    //       we need to recompute the parent, which has a constant cost in
    //       terms of arithmetic instructions.  Why can't we cache the parent?
    tree->parent(this)->arrive_internal(n);

    // clear the tentative bit at the leaf node
    MAKE_SOSIWMINIM64_WORD(temp, STEADY, n, 0);
    word.all = temp.all;
    WBR;
}


/**
 *  This code propagates the arrival of value 'n' from a child of /this/ to
 *  /this/ node, and possibly recurses to push the arrival further upward.
 */
template <int W, int D>
void sosiwminim64_node_t<W, D>::arrive_internal(int32_t n)
{
    // The first step is to determine if our arrival with a value of 'n', at a
    // decendent of /this/, means that we must change the value of this node.
    // In the ideal case, we don't need to change the value, because this node
    // has a value that is STEADY and <= 'n'.  If that is the case, this loop
    // will lead to us returning immediately.  Note, however, that we must
    // modify /this/ by incrementing the version number, to avoid a race.
    sosiwminim64_word_t x;

    x.all = revisit(this);
    if (x.fields.min <= n && x.fields.state == STEADY)
        return;

    // if n < this.word.val, then we need to use a CAS to update this node so
    // that its value == n, and so that it is TENTATIVE
    while (true) {
        // reread word
        //
        // [mfs] could change logic so that we re-read the word only if we fail
        //       in the following CAS, to reduce the frequency of mvx
        //       instructions.  Then again, if we did cas64 instead of bcas64,
        //       we'd get the atomic re-read for free...
        mvx(&word.all, &x.all);

        // if we are not modifying the value, then we can exit this loop
        if (n >= x.fields.min)
            break;

        sosiwminim64_word_t temp;
        MAKE_SOSIWMINIM64_WORD(temp, TENTATIVE, n, x.fields.ver + 1);
        if (bcas64(&word.all, x.all, temp.all)) {
            x.all = temp.all;
            break;
        }
    }

    // In the common case, the node is TENTATIVE, indicating that some
    // arriver (maybe not me) has updated this node.  We need to propagate the
    // value of this node upward, so that we are either (a) propagating our own
    // value up, or (b) propagating a concurrent arriver up.  If we don't do
    // this, then a future query by this thread will violate processor
    // consistency, by appearing to happen before this arrive().
    if (x.fields.state == TENTATIVE) {
        // first, we recurse upward to arrive at the parent
        //
        // [mfs] the is_root computation is unnecessary arithmetic.  Wouldn't a
        //       locally cached copy of 'parent' suffice to figure this out?
        if (!tree->is_root(this))
            // [mfs] The computation of parent is unnecessary arithmetic.  We
            //       should cache it in a field of /this/.
            //
            // [mfs] we need to rewrite this code to avoid recursion... it
            //       isn't going to be especially easy, given that we need to
            //       traverse back down the tree.  Perhaps we could keep a
            //       'stack' as a local variable, by storing an array whose
            //       size is known at compile-time to be the depth of the tree?
            tree->parent(this)->arrive_internal(n);

        // Once we have successfully propagated upward, we can clear the
        // tentative mark from this node, and then return, which will allow us
        // to clear the tentative mark of descendents.
        //
        // [mfs] It seems that we only clear the tentative mark from a node if
        //       its value is the one that we are putting into the Mindicator.
        //       Otherwise, the (presumably delayed) concurrent writer will
        //       need to clear that flag later.  Is this going to create
        //       pathologies, where we must propagate actions up the tree
        //       without actually doing modications to values, only because
        //       there is a concurrent TENTATIVE action that is delayed?
        if (x.fields.min == n) {
            // [mfs] I really don't like it that we say 'n' here, instead of
            //       x.fields.min.  I know they are equal, but every time I see
            //       it I think there is a bug.
            sosiwminim64_word_t temp;
            MAKE_SOSIWMINIM64_WORD(temp, STEADY, n, x.fields.ver + 1);
            // [mfs] it is interesting to note that we do not need a 'while'
            //       loop around this CAS.  Since the x86 and SPARC guarantee
            //       progress for a CAS, we know that a failure must mean that
            //       the version number has changed, in which case we are
            //       competing with another concurrent operation, and that we
            //       can leave without modifying this node.
            //
            // [mfs] With that said, there are three optimizations to consider
            //       here.  First, we might want to test before the CAS, so
            //       that we can avoid the operation if it is certain to fail.
            //
            //       Second, it would be GREAT if we could avoid doing 2 CASes
            //       on the root node.  At the entry to this function, we could
            //       special-case it for the root node, in order to only do one
            //       CAS.
            //
            //       Third, as above we could be really dirty and use a 32-bit
            //       CAS that only operates on the portion of the pointer that
            //       is the version and state
#ifdef STM_CPU_SPARC
            bcas64(&word.all, x.all, temp.all);
#else
            bcas32((uint32_t*)&word.all, (uint32_t)x.all, (uint32_t)temp.all);
#endif
        }
    }
}

/**
 *  Public interface for depart.  First depart from the appropriate per-thread
 *  leaf, and then propagate the departure up toward the root.
 */
template <int W, int D>
void sosiwminim64_node_t<W, D>::depart()
{
    // we will need a copy of the original value
    int32_t n = word.fields.min;

    // write max at the leaf node... no CAS required
    word.fields.min = MAX;
    WBR;

    // update the parent
    //
    // [mfs] there is redundancy in this code, as we are computing the parent
    //       on every depart
    depart_internal(tree->parent(this), n);
}

/**
 *  This is the SOSI depart code...  What is the high-level algorithm?
 */
template <int W, int D>
void sosiwminim64_node_t<W, D>::depart_internal(sosiwminim64_node_t<W, D> * first, int32_t n)
{
    sosiwminim64_node_t<W, D> * curr = first;

    while (true) {
        // compute the min value of children
        sosiwminim64_word_t x;
        x.all = revisit(curr);

        // If the departure being propagated (as defined by the value 'n') did not
        // cause the value of this node to change (i.e., the min child is not
        // departing), then we are done unless there is a concurrent arrive that
        // has made this node TENTATIVE.  If there is a concurrent arrive, then it
        // could be masking the fact that we must propagate our departure upward,
        // and thus we must propagate anyway, or else future queries may not
        // linearize.
        if ((x.fields.min < n) && (x.fields.state == STEADY))
            return;

        // Propagate the depart up to the parent
        //
        // [mfs] We need to avoid recursion here.  The recursion is going to cause
        //       unavoidable overhead for function calls.  This method will need to
        //       become static if we are to avoid the recursion.
        //
        // [mfs] Again, redundant computation of parent
        if (curr->tree->is_root(curr))
            return;

        curr = curr->tree->parent(curr);
    }
}

/**
 * Re-compute the min value of children, return true if the value
 * is changed.
 */
template <int W, int D>
uint64_t sosiwminim64_node_t<W, D>::revisit(sosiwminim64_node_t<W, D> * curr)
{
    int count = 0;

    while (true) {
        // the word is volatile... get a safe copy of it via 64-bit atomic load
        sosiwminim64_word_t x;
        mvx(&curr->word.all, &x.all);

        // if the node is tentative, it means one of my peers is propagating an
        // arrive up the chain.  By returning right here, we'll climb up, which
        // ensures that we will see a steady node.
        //
        // [mfs] I am not certain about this logic, but I think that since we
        //       are departing, and thus putting INT_MAX into our own leaf, the
        //       tentative state indicates that this node is already updated
        //       appropriately (either the arriving number is < our old value,
        //       or else the state flipped to TENTATIVE after we wrote INT_MAX
        //       into our leaf).  The need to return and propagate upward stems
        //       from the fact that the arrive may be hiding the fact that we
        //       need to propagate our departure upward (e.g., if this node's
        //       parent still thinks that I am the min; that's the 'state
        //       flipped' clause from above), or else we lose linearizability
        if (x.fields.state == TENTATIVE)
            return x.all;

        // compute mvc: min value of children
        //
        // NB: we don't need to do atomic 64-bit reads if we are only working
        //     with an aligned 32-bit field within the packed struct
        //
        // [mfs] there is some redundancy in this code.  Why can't we cache the
        //       begin and end values as fields of a node, so that we don't
        //       have to do the arithmetic to re-compute them on every revisit?
        sosiwminim64_node_t<W, D>* begin = curr->tree->children(curr);
        sosiwminim64_node_t<W, D>* end = begin + curr->tree->WAY;
        int32_t mvc = begin->word.fields.min;  // min value of all children
        for (sosiwminim64_node_t<W, D> * n = begin + 1; n < end; n++) {
            int32_t lmin = n->word.fields.min;
            if (mvc > lmin)
                mvc = lmin;
        }

        // if the minimum value over all children is less than the current
        // value of this node, then this is an intermediate node, and there is
        // an arriver in-flight.  We need to help the arriver, who is
        // tentative.
        //
        // [mfs] since x.fields.state != TENTATIVE, I think we know a little
        //       bit more.  It must be the case that the arriver is in the
        //       process of trying to modify this node, as it must have already
        //       modified one of the children, and so must now be working on
        //       this.  That being the case, it might make more sense for us to
        //       spin briefly, then wait to see if word.all has changed from
        //       x.all, because we really don't want to do a CAS if we don't
        //       have to, and if arrive operations are faster (they may be,
        //       especially for large W, since this operation is touching all W
        //       children), then in the common case, we are going to fail on
        //       our CAS right here.
        if (mvc < x.fields.min) {
            // [mfs] I'm guessing that what this means is that we are
            //       propagating up an in-flight arriver, which means that we
            //       have to put the arriver's value in this node and mark it
            //       as tentative so that the arrive will proceed properly.  Of
            //       course, if the arriver is truly delayed, then we are
            //       probably going to end up propagating the arrive as far up
            //       as it will go.
            //
            // [mfs] Is there a pathology here?  If I propagate the arrive all
            //       the way up, and I leave this node as tentative, then have
            //       I just committed all future operations to take a slow
            //       path, until the tentative arriver awakes and finishes?
            sosiwminim64_word_t temp;
            MAKE_SOSIWMINIM64_WORD(temp, TENTATIVE, mvc, x.fields.ver + 1);
            // [mfs] bcas64 is expensive... perhaps we should verify that
            //       word.all == x.all before trying?
            if (bcas64(&curr->word.all, x.all, temp.all))
                return temp.all;
        }
        // the minimum value over all children is >= the current value of the
        // node.  The original comment was that we need to "lift it up"
        //
        // [mfs] I think this means that the departing thread was the previous
        //       min.  In that case, we are changing this value by increasing
        //       it, which means that a parent may have a copy of this value
        //       and need to be updated, too.  The '=' condition is annoying,
        //       as it would seem that in the case of '=', no CAS would be
        //       needed, but I think yjl has a case in which failing to CAS on
        //       '=' (and hence failing to bump the version number) leads to
        //       ABA problems.
        else {
            // [mfs] again, we might want to check word.all first...
            sosiwminim64_word_t temp;
            MAKE_SOSIWMINIM64_WORD(temp, STEADY, mvc, x.fields.ver + 1);
            if (bcas64(&curr->word.all, x.all, temp.all))
                return temp.all;
        }

        if (++count >= 2) {
            mvx(&curr->word.all, &x.all);
            return x.all;
        }
    }
}
} // namespace mindicator

#endif
