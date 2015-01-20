#pragma once

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <x86intrin.h>

#include "common.hpp"
#include "mm.hpp"

using std::atomic;
using std::stringstream;
using std::string;
using std::endl;
using std::memory_order;

#define MAX_ATTEMPT_NUM_MICRO 4

class moundpq_htm_t
{
  private:

    const static int32_t VAL_MIN = std::numeric_limits<int32_t>::min();
    const static int32_t VAL_MAX = std::numeric_limits<int32_t>::max();

    /** Simple linked list used to keep a per-thread pool of free mound nodes */
    struct mound_list_t
    {
        int32_t        data;
        mound_list_t * next;
    };

    union alignas(8) mound_word_t {
        struct {
            void *   ptr;
            uint32_t owned   : 1;  // which pointer we should use
            uint32_t cavity  : 1;  // whether node is cavity
            uint32_t version : 30; // monotonic timestamp
        } fields;
        uint64_t all;   // read 64-bit at once
    };

    enum mound_owner_status_t {
        OK_C2S2, TRY_C2S2, FAIL_C2S2,
        OK_C2S1, TRY_C2S1, FAIL_C2S1,
    };

    union mound_owner_status_word_t {
        struct {
            uint32_t s : 3;  // we use 3 bits for status
            uint32_t v : 29; // the rest (29 bits) for timestamp
        } fields;
        uint32_t all;
    };

    /** Ownership record for mound. */
    struct mound_owner_t {
        atomic<uint64_t> * a;
        mound_word_t a_old;
        mound_word_t a_new;
        atomic<uint64_t> * b;
        mound_word_t b_old;
        mound_word_t b_new;
        atomic<uint32_t> status;
    };

    /** Position of a node in mound, represented by level and offset. */
    struct alignas(8) mound_pos_t {
        uint32_t level;
        uint32_t index;
    };

    static mound_list_t * alloc_list()
    {
        auto * list = (mound_list_t*)wbmm_alloc(sizeof(mound_list_t));
        return list;
    }

    static void free_list(mound_list_t * ptr)
    {
        wbmm_free_safe(ptr);
    }

    static inline void MAKE_MOUND_NODE(mound_word_t & _var, void * _l, uint32_t _c, uint32_t _v)
    {
        _var.fields.ptr = _l;
        _var.fields.cavity = _c;
        _var.fields.version = _v;
        _var.fields.owned = false;
    }

    static inline void MAKE_OWNED_WORD(mound_word_t & _var, void * _o, uint32_t _v)
    {
        _var.fields.ptr = _o;
        _var.fields.cavity = 0;
        _var.fields.version = _v;
        _var.fields.owned = true;
    }

  private:

    // per-thread "transaction" my_tx
    static thread_local mound_owner_t my_tx;

    // for picking random leaves
    static thread_local uint32_t my_seed;

  private:

    // We implement mound as an array of levels. Level i holds
    // 2^i elements.
    atomic<atomic<uint64_t> *> levels[32];

    // The index of the level that currently is the leaves
    atomic<uint32_t> bottom;

  private:

    bool C2S2(atomic<uint64_t> * a, mound_word_t a_old, mound_word_t a_new,
              atomic<uint64_t> * b, mound_word_t b_old, mound_word_t b_new)
    {
        // make orec: copy parameters
        mound_owner_t * o = &my_tx;
        o->a = a;
        o->a_old.all = a_old.all;
        o->a_new.all = a_new.all;
        o->b = b;
        o->b_old.all = b_old.all;
        o->b_new.all = b_new.all;

        mound_owner_status_word_t os;
        os.all = o->status;
        os.fields.s = TRY_C2S2;
        o->status = os.all;

        mound_word_t a1, a2, b1, b2;
        MAKE_OWNED_WORD(a1, o, a_old.fields.version);
        MAKE_OWNED_WORD(b1, o, b_old.fields.version);

        mound_owner_status_word_t s_ok, s_fail;
        s_ok.fields.s = OK_C2S2;
        s_ok.fields.v = os.fields.v + 1;
        s_fail.fields.s = FAIL_C2S2;
        s_fail.fields.v = os.fields.v + 1;

        uint64_t temp;
        mound_word_t x;

        // op is invisible until I install A
        temp = a_old.all;
        if (!bcas(a, &temp, a1.all))
            return false;

        bool succ; // whether the C2S2 can succeed

        // attempt to acquire b, if we succeed, the C2S2 succeed
        temp = b_old.all;
        if (bcas(b, &temp, b1.all)) {
            succ = true;
            x.all = b1.all;//temp
            o->status = s_ok.all;
        }
        // someone helped me with the last cas, so I have linearized
        else if (x.all = temp, x.fields.ptr == (void *)o) {
            succ = true;
            o->status = s_ok.all;
        }
        else if (os.all = o->status, os.fields.s == OK_C2S2) {
            return true;
        }
        else {
            succ = false;
            o->status = s_fail.all;
        }

        if (succ) {
            // once the second CAS succeeds, the C2S2 is done.  The status is OK_C2S2
            // by the time we're here, so anyone can help clean up.  That means we need
            // CASes.  Cleanup order does not matter.  May want to use test-and-CAS?
            MAKE_MOUND_NODE(a2, a_new.fields.ptr, a_new.fields.cavity, a_old.fields.version + 1);
            MAKE_MOUND_NODE(b2, b_new.fields.ptr, b_new.fields.cavity, b_old.fields.version + 1);
            bcas(a, &a1.all, a2.all);
            bcas(b, &b1.all, b2.all);
        }
        else {
            // rollback a to old state
            MAKE_MOUND_NODE(a2, a_old.fields.ptr, a_old.fields.cavity, a_old.fields.version + 1);
            bcas(a, &a1.all, a2.all);
        }

        return succ;
    }

    // Now our helper function does not take use of the status
    // field in the owner cache. so in all cases, we start the helping
    // from the acquiring CAS on b. the code is duplicated from the
    // second part of regular c2s2 code, but note the difference that
    // we have to use cas on updating status so that an "expired" operation
    // will not cause races.
    void C2S2_HELPER(mound_owner_t * o, mound_owner_t & cache)
    {
        // we can recover the parameters of c2s2 from the cache of orec
        atomic<uint64_t>
            * a = cache.a, * b = cache.b;
        mound_word_t
            a_old = cache.a_old, a_new = cache.a_new,
            b_old = cache.b_old, b_new = cache.b_new;

        mound_word_t a1, a2, b1, b2;
        MAKE_OWNED_WORD(a1, o, a_old.fields.version);
        MAKE_OWNED_WORD(b1, o, b_old.fields.version);

        mound_owner_status_word_t s_ok, s_fail, os, os2;
        os.all = cache.status;
        s_ok.fields.s = OK_C2S2;
        s_ok.fields.v = os.fields.v + 1;
        s_fail.fields.s = FAIL_C2S2;
        s_fail.fields.v = os.fields.v + 1;

        /*** The following piece of code is copied from the second part of C2S2 ***/
        bool succ; // whether the C2S2 can succeed
        uint64_t temp;
        mound_word_t x;

        //return; // temp
        temp = b_old.all;

        if (bcas(b, &temp, b1.all)) {
            succ = true;
            bcas(&o->status, &os.all, s_ok.all);
        }
        else if (x.all = temp, x.fields.ptr == (void *)o) {
            succ = true;
            bcas(&o->status, &os.all, s_ok.all);
        }
        else if (os2.all = o->status, os2.fields.s == OK_C2S2) {
            return;
        }
        else {
            succ = false;
            bcas(&o->status, &os.all, s_fail.all);
        }
        if (succ) {
            MAKE_MOUND_NODE(a2, a_new.fields.ptr, a_new.fields.cavity, a_old.fields.version + 1);
            MAKE_MOUND_NODE(b2, b_new.fields.ptr, b_new.fields.cavity, b_old.fields.version + 1);
            bcas(a, &a1.all, a2.all);
            bcas(b, &b1.all, b2.all);
        }
        else {
            MAKE_MOUND_NODE(a2, a_old.fields.ptr, a_old.fields.cavity, a_old.fields.version + 1);
            bcas(a, &a1.all, a2.all);
        }
        /*** The above piece of code is copied from the second part of C2S2 ***/
    }

    __attribute__((noinline))
    uint64_t READ_HELPMODE(atomic<uint64_t> * addr)
    {
        while (true) {
            spin64();
            // atomic read of the node
            mound_word_t v;
            v.all = *addr;

            // common case: v is not owned
            if (__builtin_expect(!v.fields.owned, true)) return v.all;

            // ick.  I have to help.  First step: snapshot the owner
            mound_owner_t oc;
            mound_owner_t * o = (mound_owner_t *)v.fields.ptr;
            memcpy(&oc, v.fields.ptr, sizeof(mound_owner_t));

            // double check that owner still installed, else snapshot invalid
            // if the timestamp is not changed, we know the orec is not reclaimed
            // (so never re-allocated)
            // also note that status is the only mutable field in an orec, so a
            // single read can give us the snapshot
            mound_word_t v1;
            v1.all = *addr;

            if (v1.all != v.all) continue;

            C2S2_HELPER((mound_owner_t *)v.fields.ptr, oc);
        }
    }

    inline uint64_t ATOMIC_READ(mound_pos_t pos)
    {
        atomic<uint64_t> * addr = &levels[pos.level][pos.index];
        mound_word_t v;
        v.all = *addr;
        if (__builtin_expect(!v.fields.owned, true)) return v.all;
        return READ_HELPMODE(addr);
    }

    inline bool ATOMIC_CAS(mound_pos_t N, mound_word_t NN, mound_word_t NN_new)
    {
        auto nodeptr = &levels[N.level][N.index];
        return bcas(nodeptr, &NN.all, NN_new.all);
    }

    __attribute__((noinline))
    bool ATOMIC_C2S1(mound_pos_t C, mound_word_t CC, mound_word_t CC_new,
                            mound_pos_t P, mound_word_t PP)
    {
        auto child = &levels[C.level][C.index];
        auto parent = &levels[P.level][P.index];

        uint32_t attempts = 0;
        uint32_t status;
        bool ok = false;
      retry:
        status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            if (levels[C.level][C.index] == CC.all && levels[P.level][P.index] == PP.all) {
                levels[C.level][C.index] = CC_new.all;
                ok = true;
            }
            _xend();
            return ok;
        }
        else {
            if (++attempts < MAX_ATTEMPT_NUM_MICRO) {
                goto retry;
            }
        }
        return C2S2(child, CC, CC_new, parent, PP, PP);
    }

    __attribute__((noinline))
    bool ATOMIC_C2S2(mound_pos_t P, mound_word_t PP, mound_word_t PP_new,
                     mound_pos_t C, mound_word_t CC, mound_word_t CC_new)
    {
        auto child = &levels[C.level][C.index];
        auto parent = &levels[P.level][P.index];

        uint32_t attempts = 0;
        uint32_t status;
        bool ok = false;
      retry1:
        status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            if (levels[C.level][C.index] == CC.all && levels[P.level][P.index] == PP.all) {
                levels[C.level][C.index] = CC_new.all;
                levels[P.level][P.index] = PP_new.all;
                ok = true;
            }
            _xend();
        }
        else {
            if (++attempts < MAX_ATTEMPT_NUM_MICRO) {
                goto retry1;
            }
            ok = C2S2(child, CC, CC_new, parent, PP, PP_new);
        }
        return ok;
    }

  private:

    /***  Indicate whether the given node is leaf. */
    inline bool is_leaf(mound_pos_t node)
    {
        return node.level == bottom;
    }

    /***  Indicate whether the given node is root. */
    inline bool is_root(mound_pos_t node)
    {
        return node.level == 0;
    }

    /*** Get the parent position of given node. */
    inline mound_pos_t parent_of(mound_pos_t node)
    {
        mound_pos_t result;
        result.level = node.level - 1;
        result.index = node.index / 2;
        return result;
    }

    /*** Get the left child position of given node. */
    inline mound_pos_t left_of(mound_pos_t node)
    {
        mound_pos_t result;
        result.level = node.level + 1;
        result.index = node.index * 2;
        return result;
    }

    /*** Get the right child position of given node. */
    inline mound_pos_t right_of(mound_pos_t node)
    {
        mound_pos_t result;
        result.level = node.level + 1;
        result.index = node.index * 2 + 1;
        return result;
    }

    /** Extend an extra level for the mound. */
    void grow(uint32_t btm)
    {
        if (bottom != btm) return;
        atomic<uint64_t> * oldlevel = levels[btm + 1];

        if (oldlevel == NULL) {
            uint32_t size = 1 << (btm + 1);
            atomic<uint64_t> * newlevel = (atomic<uint64_t> *)malloc(size * sizeof(atomic<uint64_t>));
            memset(newlevel, 0, size * sizeof(atomic<uint64_t>));
            if (!bcas(&levels[btm + 1], &oldlevel, newlevel))
                free(newlevel);
        }

        bcas(&bottom, &btm, btm + 1);
    }

    /** Pick a node >= n*/
    mound_pos_t select_node(int32_t n, mound_word_t * NN)
    {
        while (true) {
            // NB: my_seed is a contention hotspot!
            uint32_t index = rand_r_32(&my_seed);
            uint32_t b = bottom;
            int l = 8;
            // use linear probing from a randomly selected point
            for (int i = 0; i < l; ++i) {
                int ii = (index + i) % (1 << b);
                mound_pos_t N;
                N.level = b;
                N.index = ii;
                NN->all = ATOMIC_READ(N);
                mound_list_t * LL = (mound_list_t *)NN->fields.ptr;
                int32_t nv = (LL == NULL) ? VAL_MAX : LL->data;
                // found a good node, so return
                if (nv >= n) return N;
                // stop probing if mound has been expanded
                if (b != bottom) break;
            }
            // if we failed too many times, grow the mound
            grow(b);
        }
    }

  public:

    /** Constructor zeroes all but the first level, which has a single element */
    moundpq_htm_t()
    {
        bottom = 0;
        my_seed = 0;

        levels[0] = (atomic<uint64_t> *)malloc(sizeof(atomic<uint64_t>));
        levels[0][0] = 0;

        for (int i = 1; i < 32; i++) levels[i] = NULL;
    }

    void add(int32_t n)
    {
        wbmm_begin();
        mound_pos_t C, P, M;
        mound_word_t CC, PP, MM;

        while (true) {
            // pick a random leaf >= n (cache is written in CC)
            C = select_node(n, &CC);
            // P is initialized to root
            P.level = P.index = 0;

            while (true) {
                M.level = (C.level + P.level) / 2;
                M.index = C.index >> (C.level - M.level);
                MM.all = ATOMIC_READ(M);
                int32_t mv = (MM.fields.ptr == NULL) ? VAL_MAX : ((mound_list_t *)MM.fields.ptr)->data;
                if (n > mv) {
                    P = M;
                    PP.all = MM.all;
                }
                else { // n <= mv
                    C = M;
                    CC.all = MM.all;
                }
                if (M.level == 0) break;
                if (P.level + 1 == C.level && P.level != 0) break;
            }

            // prepare to insert a new node on head of child
            mound_list_t * newlist = alloc_list();
            newlist->data = n;
            newlist->next = (mound_list_t *)CC.fields.ptr;
            mound_word_t CC_new;
            MAKE_MOUND_NODE(CC_new, newlist, CC.fields.cavity, CC.fields.version+1);

            // push n on head of root (need to ensure C <= n)
            if (is_root(C)) {
                if (ATOMIC_CAS(C, CC, CC_new)) goto exit;
            }
            // push n on child (need to ensure C <= n < P)
            else {
                if (ATOMIC_C2S1(C, CC, CC_new, P, PP)) goto exit;
            }
        }
      exit:
        wbmm_end();
    }

    int32_t remove()
    {
        wbmm_begin();

        int32_t ret;
        mound_pos_t N;
        mound_word_t NN;

        // start from the root
        N.level = N.index = 0;

        while (true) {
            // if root is cavity, fill it first
            NN.all = ATOMIC_READ(N);
            if (NN.fields.cavity)
                NN.all = fill_cavity(N);

            if (NN.fields.ptr == NULL) {
                ret = VAL_MAX;
                goto exit;
            }

            // retrieve the value from root
            mound_list_t * list = (mound_list_t *)NN.fields.ptr;
            mound_word_t NN_new;
            MAKE_MOUND_NODE(NN_new, list->next, true, NN.fields.version + 1);
            if (ATOMIC_CAS(N, NN, NN_new)) {
                ret = list->data;
                free_list(list);
                fill_cavity(N);
                goto exit;
            }
        }

      exit:
        wbmm_end();
        return ret;
    }

    uint64_t fill_cavity(mound_pos_t N)
    {
        // for caching timestamps etc
        mound_word_t NN, LL, RR;

        while (true) {
            // return if N is already not a cavity
            NN.all = ATOMIC_READ(N);
            if (!NN.fields.cavity) return NN.all;

            // if N is a leaf
            if (is_leaf(N)) {
                mound_word_t NN_new;
                MAKE_MOUND_NODE(NN_new, NN.fields.ptr, false, NN.fields.version + 1);
                if (ATOMIC_CAS(N, NN, NN_new)) return NN_new.all;
                else continue;
            }

            // Now comes the hard work.
            mound_pos_t L = left_of(N);
            mound_pos_t R = right_of(N);
            // values
            uint32_t nv, rv, lv;

            // ensure left is not cavity, otherwise fill it
            LL.all = ATOMIC_READ(L);
            if (LL.fields.cavity) LL.all = fill_cavity(L);
            // ensure right is not cavity, otherwise fill it
            RR.all = ATOMIC_READ(R);
            if (RR.fields.cavity) RR.all = fill_cavity(R);

            // compute the minimum value
            nv = (NN.fields.ptr == NULL) ? VAL_MAX : ((mound_list_t *)NN.fields.ptr)->data;
            lv = (LL.fields.ptr == NULL) ? VAL_MAX : ((mound_list_t *)LL.fields.ptr)->data;
            rv = (RR.fields.ptr == NULL) ? VAL_MAX : ((mound_list_t *)RR.fields.ptr)->data;

            // pull from right?
            if ((rv <= lv) && (rv < nv)) {
                // swap R and N lists
                mound_word_t NN_new, RR_new;
                MAKE_MOUND_NODE(NN_new, RR.fields.ptr, false, NN.fields.version + 1);
                MAKE_MOUND_NODE(RR_new, NN.fields.ptr, true,  RR.fields.version + 1);
                if (ATOMIC_C2S2(N, NN, NN_new, R, RR, RR_new)) {
                    fill_cavity(R);
                    return NN_new.all;
                }
            }
            // pull from left?
            else if ((lv <= rv) && (lv < nv)) {
                // swap L and N lists
                mound_word_t NN_new, LL_new;
                MAKE_MOUND_NODE(NN_new, LL.fields.ptr, false, NN.fields.version + 1);
                MAKE_MOUND_NODE(LL_new, NN.fields.ptr, true,  LL.fields.version + 1);
                if (ATOMIC_C2S2(N, NN, NN_new, L, LL, LL_new)) {
                    fill_cavity(L);
                    return NN_new.all;
                }
            }
            // pull from local list?
            // just clear the cavity and we're good
            else {
                mound_word_t NN_new;
                MAKE_MOUND_NODE(NN_new, NN.fields.ptr, false, NN.fields.version + 1);
                if (ATOMIC_CAS(N, NN, NN_new))
                    return NN_new.all;
            }
            // spin
            for (uint32_t i = 0; i < 64; i++) spin64();
        }
    }
};

thread_local moundpq_htm_t::mound_owner_t moundpq_htm_t::my_tx = {0};
thread_local uint32_t moundpq_htm_t::my_seed = 0;

#undef MAX_ATTEMPT_NUM_MICRO
