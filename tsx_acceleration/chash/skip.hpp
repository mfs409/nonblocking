/******************************************************************************
 * skip_cas.c
 *
 * Skip lists, allowing concurrent update by use of CAS primitives.
 *
 * Copyright (c) 2001-2003, K A Fraser
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * The name of the author may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdint>
#include <atomic>

#include "common.hpp"
#include "mm.hpp"

using std::atomic;
using std::stringstream;
using std::string;
using std::endl;

class slset_t
{
  private:

    const static int32_t VAL_MIN = std::numeric_limits<int32_t>::min();
    const static int32_t VAL_MAX = std::numeric_limits<int32_t>::max();
    const static int32_t LEVEL_MAX = 20;

    struct slnode_t
    {
        int32_t key;
        int32_t toplevel;
        atomic<uint32_t>   mark;
        atomic<slnode_t *> nexts[LEVEL_MAX];
    };

    slnode_t * head;
    slnode_t * tail;

    static thread_local uint32_t seed;

  private:

    /* 1 <= level <= LEVELMAX */
    static int get_rand_level()
    {
        int r = rand_r_32(&seed);
        int l = 1;
        r = (r >> 4) & ((1 << (LEVEL_MAX-1)) - 1);
        while ( (r & 1) ) { l++; r >>= 1; }
        return (l);
    }

    static slnode_t * alloc_node(uint32_t val, slnode_t *next, uint32_t toplevel)
    {
        slnode_t *node = (slnode_t*)wbmm_alloc(sizeof(slnode_t));
        node->key = val;
        node->toplevel = toplevel;
        node->mark = 0;
        for (int i = 0; i < LEVEL_MAX; i++)
            node->nexts[i] = next;
        return node;
    }

    static void free_node_safe(slnode_t * ptr)
    {
        wbmm_free_safe(ptr);
    }

    static void free_node_unsafe(slnode_t * ptr)
    {
        wbmm_free_unsafe(ptr);
    }

  public:

    slset_t()
    {
        tail = alloc_node(VAL_MAX, NULL, LEVEL_MAX);
        head = alloc_node(VAL_MIN, tail, LEVEL_MAX);
    }

    bool insert(int key)
    {
        wbmm_begin();

        slnode_t
            * NEW = NULL, * new_next,
            * pred, * succ,
            * succs[LEVEL_MAX], * preds[LEVEL_MAX];
        bool result;

        succ = search_weak(key, preds, succs);
      retry:
        if (succ->key == key) {
            if (NEW) free_node_unsafe(NEW);
            result = false;
            goto exit;
        }

        if (!NEW)
            NEW = alloc_node(key, NULL, get_rand_level());

        for (int i = 0; i < NEW->toplevel; i++)
            NEW->nexts[i] = succs[i];

        /* Node is visible once inserted at lowest level */
        if (!bcas(&preds[0]->nexts[0], &succ, NEW)) {
            succ = search(key, preds, succs);
            goto retry;
        }

        result = true;

        for (int i = 1; i < NEW->toplevel; i++) {
            while (true) {
                pred = preds[i];
                succ = succs[i];

                new_next = NEW->nexts[i];
                if (IS_MARKED(new_next)) goto success;

                /* Update the forward pointer if it is stale */
                if (new_next != succ) {
                    if (!bcas(&NEW->nexts[i], &new_next, succ))
                        goto success;
                }

                /* We retry the search if the CAS fails */
                if (bcas(&pred->nexts[i], &succ, NEW))
                    break;

                search(key, preds, succs);
            }
        }

      success:
        if (check_for_full_delete(NEW))
            do_full_delete(NEW, NEW->toplevel);

      exit:
        wbmm_end();
        return result;
    }

    bool remove(int key)
    {
        wbmm_begin();

        bool result = false;
        slnode_t * succ = search_weak(key, NULL, NULL);

        if (succ->key == key) {
            bool iMarkIt = mark_node_ptrs(succ);
            if (iMarkIt) {
                if (check_for_full_delete(succ)) {
                    do_full_delete(succ, succ->toplevel);
                }
                result = true;
            }
        }

        wbmm_end();
        return result;
    }


    bool contains(int key)
    {
        wbmm_begin();
        bool result = search_weak(key, NULL, NULL)->key == key;
        wbmm_end();
        return result;
    }

    bool grow() { return false; }
    bool shrink() { return false; }

  private:

    static bool check_for_full_delete(slnode_t * x)
    {
        uint32_t mark = x->mark;
        return (mark == 1 || !bcas(&x->mark, &mark, (uint32_t)1));
    }

    void do_full_delete(slnode_t * x, int level)
    {
        search(x->key, NULL, NULL);
        free_node_safe(x);
    }

    slnode_t * search_weak(int key, slnode_t **left_list, slnode_t **right_list)
    {
        slnode_t *left, *left_next, *right, *right_next;
      retry:
        left = head;
        for (int i = LEVEL_MAX - 1; i >= 0; i--) {
            left_next = (slnode_t *)REF_UNMARKED(left->nexts[i].load());
            /* Find unmarked node pair at this level */
            for (right = left_next; ; right = right_next) {
                /* Skip a sequence of marked nodes */
                right_next = right->nexts[i];
                while (IS_MARKED(right_next)) {
                    right = (slnode_t *)REF_UNMARKED(right_next);
                    right_next = right->nexts[i];
                }
                if (right->key >= key) break;
                left = right;
                left_next = right_next;
            }
            if (left_list != NULL) left_list[i] = left;
            if (right_list != NULL) right_list[i] = right;
        }
        return right;
    }

    slnode_t * search(int key, slnode_t **left_list, slnode_t **right_list)
    {
        slnode_t *left, *left_next, *right, *right_next;
      retry:
        left = head;
        for (int i = LEVEL_MAX - 1; i >= 0; i--) {
            left_next = left->nexts[i];
            if (IS_MARKED(left_next))
                goto retry;
            /* Find unmarked node pair at this level */
            for (right = left_next; ; right = right_next) {
                /* Skip a sequence of marked nodes */
                right_next = right->nexts[i];
                while (IS_MARKED(right_next)) {
                    right = (slnode_t *)REF_UNMARKED(right_next);
                    right_next = right->nexts[i];
                }
                if (right->key >= key)
                    break;
                left = right;
                left_next = right_next;
            }

            /* Ensure left and right nodes are adjacent */
            if (left_next != right)
                if (!bcas(&left->nexts[i], &left_next, right))
                    goto retry;
            if (left_list != NULL) left_list[i] = left;
            if (right_list != NULL) right_list[i] = right;
        }
        return right;
    }

    static bool mark_node_ptrs(slnode_t * n)
    {
        slnode_t * n_next;
        bool result;
        for (int i = n->toplevel-1; i >= 0; i--) {
            do {
                n_next = n->nexts[i];
                if (IS_MARKED(n_next)) {
                    result = false;
                    break;
                }
                if (bcas(&n->nexts[i], &n_next, (slnode_t *)REF_MARKED(n_next))) {
                    result = true;
                    break;
                }
            } while (true);
        }
        return result;
    }
};

thread_local uint32_t slset_t::seed = 0;

