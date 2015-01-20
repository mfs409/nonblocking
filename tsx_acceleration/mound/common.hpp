///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2011
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

/** Common code for mounds. */

#ifndef MOUND_COMMON_HPP__
#define MOUND_COMMON_HPP__

#define HEAP_MAX_SIZE 128 * 1024 * 1024

/**
 * Simple linked list used to keep a per-thread pool of free mound nodes
 */
struct mound_list_t
{
    volatile uint32_t       data;
    mound_list_t * volatile next;
};

/** Forward declaration of mound_owner_t. */
struct mound_owner_t;

/**
 *  Read/write a mound node through a node cache object.
 */
union mound_word_t
{
    volatile struct
    {
        union
        {
            mound_list_t*  list;         // 32-bit pointer to head
            mound_owner_t* owner;        // 32-bit pointer to owner
        };
        // [lyj] we can use the spare lower bits of pointer to store
        // the following two bits
        uint32_t       owned   : 1;  // which pointer we should use
        uint32_t       cavity  : 1;  // whether node is cavity
        uint32_t       version : 30; // monotonic timestamp
    } fields;
    volatile uint64_t all;   // read 64-bit at once
}  __attribute__ ((aligned(8)));

/*** HELPER MACROS */
#define MAKE_WORD(_var, _l, _c, _v)                                     \
    { _var.fields.list = _l; _var.fields.cavity = _c; _var.fields.version = _v; _var.fields.owned = false; }

#define MAKE_OWNED_WORD(_var, _o, _v)                                   \
    { _var.fields.owner = _o; _var.fields.cavity = 0; _var.fields.version = _v; _var.fields.owned = true; }

enum mound_owner_status_t
    {
        OK_C2S2, TRY_C2S2, FAIL_C2S2,
        OK_C2S1, TRY_C2S1, FAIL_C2S1,
    };

union mound_owner_status_word_t
{
    volatile struct
    {
        uint32_t s : 3;  // we use 3 bits for status
        uint32_t v : 29; // the rest (29 bits) for timestamp
    } fields;
    volatile uint32_t all;
};

/** Ownership record for mound. */
struct mound_owner_t
{
    mound_word_t* volatile a;
    mound_word_t a_old;
    mound_word_t a_new;

    mound_word_t* volatile b;
    mound_word_t b_old;
    mound_word_t b_new;

    mound_owner_status_word_t status;
};

/** Position of a node in mound, represented by level and offset. */
struct mound_pos_t
{
    uint32_t level;
    uint32_t index;
};

// Skip list type and varialble definitions

const uint32_t LEVELMAX = 27;
__thread uint32_t fraser_seed = 0;
volatile uint32_t sl_counter = 0;

#define VAL_MIN                         0
#define VAL_MAX                         UINT_MAX
#define SL_GET_TIME()                   fai32(&sl_counter)

struct sl_node_t
{
    volatile uint32_t val;
    volatile uint32_t deleted;
    volatile uint32_t ts;
    uint32_t toplevel;
    sl_node_t *volatile nexts[LEVELMAX];
    sl_node_t *volatile next; // used by the pool
};

struct sl_intset_t
{
    sl_node_t *head;
    sl_node_t *tail;
};

namespace mound_list_pool
{
#define TYPE mound_list_t
#define POOL_SIZE 65536
#define LIST_SIZE 0
#include "../common/pool.src"
#undef TYPE
#undef POOL_SIZE
#undef LIST_SIZE
}

namespace sl_node_pool
{
#define TYPE sl_node_t
#define POOL_SIZE 65536
#define LIST_SIZE 65536
#include "../common/pool.src"
#undef TYPE
#undef POOL_SIZE
#undef LIST_SIZE
}

// per-thread "transaction" descriptor
__thread mound_owner_t _tx_descriptor = {0};

// for picking random leaves
__thread uint32_t seed = 0;


bool C2S2(mound_word_t* a, mound_word_t a_old, mound_word_t a_new,
          mound_word_t* b, mound_word_t b_old, mound_word_t b_new)
{
    // make orec: copy parameters
    mound_owner_t* o = &_tx_descriptor;
    o->a = a;
    o->a_old.all = a_old.all;
    o->a_new.all = a_new.all;
    o->b = b;
    o->b_old.all = b_old.all;
    o->b_new.all = b_new.all;
    o->status.fields.s = TRY_C2S2;

    mound_word_t a1, a2, b1, b2;
    MAKE_OWNED_WORD(a1, o, a_old.fields.version);
    MAKE_OWNED_WORD(b1, o, b_old.fields.version);

    mound_owner_status_word_t s_ok, s_fail;
    s_ok.fields.s = OK_C2S2;
    s_ok.fields.v = o->status.fields.v + 1;
    s_fail.fields.s = FAIL_C2S2;
    s_fail.fields.v = o->status.fields.v + 1;

    // op is invisible until I install A
    if (!bcas64(&a->all, a_old.all, a1.all))
        return false;

    bool succ; // whether the C2S2 can succeed

    // attempt to acquire b, if we succeed, the C2S2 succeed
    if (bcas64(&b->all, b_old.all, b1.all)) {
        succ = true;
        // if the status is already OK, we will 'overwrite' it
        // with identical value
        o->status.all = s_ok.all;
    }
    // someone helped me with the last cas, so I have linearized
    else if (b->fields.owner == o) {
        succ = true;
        // if the status is already OK, we will 'overwrite' it
        // with identical value
        o->status.all = s_ok.all;
    }
    // Otherwise it becomes tricky, since we have to distinguish
    // between two possibilities:
    // Case 1: b is acquired by another conflicting operation
    //   after a is acquired by me.
    // Case 2: b is acquired by a helper. the helper has made
    //   a lot of progress (has cleaned up both a and b).

    // How can we distinguish between the above 2 cases?
    // -- we can use the status field and the following properties:
    // Property 1: if it is case 2, then my status = OK_C2S2
    //   Proof. because the helper must have set status to OK
    //   before it did the clean up work.
    // Property 2: if it is case 1, then my status != OK_C2S2
    //   Proof. because the conflicting operation will set b's
    //   owner field, neither me nor any helper can succeed in
    //   the acquiring cas on b, thus nobody will observe
    //   b.owner = me, so no code path can set status to OK_C2S2.
    else if (o->status.fields.s == OK_C2S2) { // must be case 2
        // we don't need clean up in this case, we know b has been
        // cleaned up by the helper, so a is also cleaned by the
        // helper (here we rely on the order of clean up a->b)
        // assert(o->status == OK or FAIL)
        return true;
    }
    else { // must be case 1
        succ = false;
        // if the status is already FAIL, we will 'overwrite' it
        // with identical value
        o->status.all = s_fail.all;
    }

    // assert(o->status == OK or FAIL)

    if (succ) {
        // once the second CAS succeeds, the C2S2 is done.  The status is OK_C2S2
        // by the time we're here, so anyone can help clean up.  That means we need
        // CASes.  Cleanup order does not matter.  May want to use test-and-CAS?
        MAKE_WORD(a2, a_new.fields.list, a_new.fields.cavity, a_old.fields.version + 1);
        MAKE_WORD(b2, b_new.fields.list, b_new.fields.cavity, b_old.fields.version + 1);
        cas64(&a->all, a1.all, a2.all);
        cas64(&b->all, b1.all, b2.all);
    }
    else {
        // rollback a to old state
        MAKE_WORD(a2, a_old.fields.list, a_old.fields.cavity, a_old.fields.version + 1);
        cas64(&a->all, a1.all, a2.all);
    }

    return succ;
}

// [lyj] now our helper function does not take use of the status
// field in the owner cache. so in all cases, we start the helping
// from the acquiring CAS on b. the code is duplicated from the
// second part of regular c2s2 code, but note the difference that
// we have to use cas on updating status so that an "expired" operation
// will not cause races.
void C2S2_HELPER(mound_owner_t * o, mound_owner_t cache)
{
    // we can recover the parameters of c2s2 from the cache of orec
    volatile mound_word_t *a = cache.a, *b = cache.b;
    mound_word_t a_old = cache.a_old, a_new = cache.a_new,
                 b_old = cache.b_old, b_new = cache.b_new;

    mound_word_t a1, a2, b1, b2;
    MAKE_OWNED_WORD(a1, o, a_old.fields.version);
    MAKE_OWNED_WORD(b1, o, b_old.fields.version);

    mound_owner_status_word_t s_ok, s_fail;
    s_ok.fields.s = OK_C2S2;
    s_ok.fields.v = cache.status.fields.v + 1;
    s_fail.fields.s = FAIL_C2S2;
    s_fail.fields.v = cache.status.fields.v + 1;

    /*** The following piece of code is copied from the second part of C2S2 ***/
    bool succ; // whether the C2S2 can succeed

    if (bcas64(&b->all, b_old.all, b1.all)) {
        succ = true;
        cas32(&o->status.all, cache.status.all, s_ok.all);
    }
    else if (b->fields.owner == o) {
        succ = true;
        cas32(&o->status.all, cache.status.all, s_ok.all);
    }
    else if (o->status.fields.s == OK_C2S2) {
        return;
    }
    else {
        succ = false;
        cas32(&o->status.all, cache.status.all, s_fail.all);
    }

    if (succ) {
        MAKE_WORD(a2, a_new.fields.list, a_new.fields.cavity, a_old.fields.version + 1);
        MAKE_WORD(b2, b_new.fields.list, b_new.fields.cavity, b_old.fields.version + 1);
        cas64(&a->all, a1.all, a2.all);
        cas64(&b->all, b1.all, b2.all);
    }
    else {
        MAKE_WORD(a2, a_old.fields.list, a_old.fields.cavity, a_old.fields.version + 1);
        cas64(&a->all, a1.all, a2.all);
    }
    /*** The above piece of code is copied from the second part of C2S2 ***/
}

void READ_HELPMODE(mound_word_t* const addr, mound_word_t* result);

void READ(mound_word_t* const addr, mound_word_t* result)
{
    mound_word_t v;
    uint32_t c1, c2;
    mvx(&addr->all, &v.all);
    if (__builtin_expect(!v.fields.owned, true)) {
        result->all = v.all;
        return;
    }
    READ_HELPMODE(addr, result);
}

__attribute__((noinline))
void READ_HELPMODE(mound_word_t* const addr, mound_word_t* result)
{
    while (true) {
        spin64();
        // atomic read of the node
        mound_word_t v;
        mvx(&addr->all, &v.all);

        // common case: v is not owned
        if (__builtin_expect(!v.fields.owned, true)) {
            result->all = v.all;
            return;
        }

        // ick.  I have to help.  First step: snapshot the owner
        mound_owner_t oc = *v.fields.owner;

        // double check that owner still installed, else snapshot invalid
        // if the timestamp is not changed, we know the orec is not reclaimed
        // (so never re-allocated)
        // also note that status is the only mutable field in an orec, so a
        // single read can give us the snapshot
        CFENCE;
        mound_word_t v1;
        mvx(&addr->all, &v1.all);

        if (v1.all != v.all)
            continue;

        C2S2_HELPER(v.fields.owner, oc);
    }
}

#endif
