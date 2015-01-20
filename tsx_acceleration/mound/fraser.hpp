/*** [lyj] The following code is adapted from the Microbench repository,
     originally written by Vincent Gramoli. */
/*
 * File:
 *   skiplist.c skiplist.h fraser.c fraser.h
 * Author(s):
 *   Vincent Gramoli <vincent.gramoli@epfl.ch>
 * Description:
 *   Skip list definition
 *
 * Copyright (c) 2009-2010.
 *
 * skiplist.c is part of Microbench
 * skiplist.h is part of Microbench
 * fraser.c is part of Microbench
 * fraser.c is part of Microbench
 *
 * Microbench is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _FRASER_HPP
#define _FRASER_HPP

#include "../common/platform.hpp"
#include "common.hpp"

#define ATOMIC_CAS_MB(a, e, v)          (bcasptr((volatile uintptr_t *)(a), (uintptr_t)(e), (uintptr_t)(v)))

/** Utility functions for marked pointers */
inline uintptr_t is_marked(uintptr_t i)
{
    return (uintptr_t)(i & 0x01);
}

inline uintptr_t unset_mark(uintptr_t i)
{
    return (i & ~0x01);
}

inline uintptr_t set_mark(uintptr_t i)
{
    return (i | 0x01);
}

int get_rand_level()
{
    uint32_t i, level = 1;
    for (i = 0; i < LEVELMAX - 1; i++) {
        if ((rand_r(&fraser_seed) % 100) < 50)
            level++;
        else
            break;
    }
    /* 1 <= level <= LEVELMAX */
    return level;
}

/*
 * Create a new node without setting its next fields.
 */
sl_node_t *sl_new_simple_node(uint32_t val, uint32_t toplevel, bool lin)
{
    //sl_node_t *node = (sl_node_t*)malloc(sizeof(sl_node_t));
    sl_node_t *node = sl_node_pool::get();
    node->val = val;
    node->toplevel = toplevel;
    node->deleted = 0;
    if (lin)
        node->ts = SL_GET_TIME();
    return node;
}

/*
 * Create a new node with its next field.
 * If next=NULL, then this create a tail node.
 */
sl_node_t *sl_new_node(uint32_t val, sl_node_t *next, uint32_t toplevel)
{
    sl_node_t *node;
    uint32_t i;
    node = sl_new_simple_node(val, toplevel, true);
    for (i = 0; i < LEVELMAX; i++)
        node->nexts[i] = next;
    return node;
}

void sl_delete_node(sl_node_t *n)
{
    sl_node_pool::put(n);
}

sl_intset_t *sl_set_new()
{
    sl_intset_t *set;
    sl_node_t *min, *max;
    if ((set = (sl_intset_t *)malloc(sizeof(sl_intset_t))) == NULL) {
        perror("malloc");
        exit(1);
    }
    max = sl_new_node(VAL_MAX, NULL, LEVELMAX);
    min = sl_new_node(VAL_MIN, max, LEVELMAX);
    set->head = min;
    set->tail = max;
    return set;
}

void sl_set_delete(sl_intset_t *set)
{
    sl_node_t *node, *next;
    node = set->head;
    while (node != NULL) {
        next = node->nexts[0];
        sl_delete_node(node);
        node = next;
    }
    free(set);
}

// Fraser's SkipList Algorithm

inline void fraser_search(sl_intset_t *set, uint32_t val, sl_node_t **left_list, sl_node_t **right_list)
{
    int i;
    sl_node_t *left, *left_next, *right, *right_next;
  retry:
    left = set->head;
    for (i = LEVELMAX - 1; i >= 0; i--) {
        left_next = left->nexts[i];
        if (is_marked((uintptr_t)left_next))
            goto retry;
        /* Find unmarked node pair at this level */
        for (right = left_next; ; right = right_next) {
            /* Skip a sequence of marked nodes */
            while(1) {
                right_next = right->nexts[i];
                if (!is_marked((uintptr_t)right_next))
                    break;
                right = (sl_node_t*)unset_mark((uintptr_t)right_next);
            }
            if (right->val >= val)
                break;
            left = right;
            left_next = right_next;
        }

        /* Ensure left and right nodes are adjacent */
        if ((left_next != right) &&
            (!ATOMIC_CAS_MB(&left->nexts[i], left_next, right)))
            goto retry;
        if (left_list != NULL)
            left_list[i] = left;
        if (right_list != NULL)
            right_list[i] = right;
    }
}

inline int mark_node_ptrs(sl_node_t *n)
{
    sl_node_t *n_next;
    for (int i=n->toplevel-1; i>0; i--) {
        do {
            n_next = n->nexts[i];
            if (is_marked((uintptr_t)n_next))
                break;
            if (ATOMIC_CAS_MB(&n->nexts[i], n_next, set_mark((uintptr_t)n_next)))
                break;
        } while (true);
    }
    do {
        n_next = n->nexts[0];
        if (is_marked((uintptr_t)n_next))
            return 0;
        if (ATOMIC_CAS_MB(&n->nexts[0], n_next, set_mark((uintptr_t)n_next)))
            return 1;
    } while (true);
}

/*
 * [lyj] Skip queue does not use return value of remove, so we omit it.
 * We also disable the modification of the "deleted" field. The field is
 * reserved only for the use of wrapper logic.
 */
void fraser_remove(sl_intset_t *set, uint32_t val)
{
    sl_node_t *succs[LEVELMAX];
    fraser_search(set, val, NULL, succs);
    if (succs[0]->val == val) {
        /* Mark forward pointers, then search will remove the node */
        int iMarkIt = mark_node_ptrs(succs[0]);
        fraser_search(set, val, NULL, NULL);
        if (iMarkIt)
            sl_delete_node(succs[0]);
    }
}

/*
 * [lyj] Skip queue does not use return value of insert, so we omit it.
 * We also disable the interpretation of "deleted" field. The field is
 * reserved only for the use of wrapper logic.
 */
void fraser_insert(sl_intset_t *set, uint32_t v, bool lin)
{
    sl_node_t *NEW, *new_next, *pred, *succ, *succs[LEVELMAX], *preds[LEVELMAX];
    uint32_t i;
    NEW = sl_new_simple_node(v, get_rand_level(), lin);
  retry:
    fraser_search(set, v, preds, succs);
    for (i = 0; i < NEW->toplevel; i++)
        NEW->nexts[i] = succs[i];
    /* Node is visible once inserted at lowest level */
    if (!ATOMIC_CAS_MB(&preds[0]->nexts[0], succs[0], NEW))
        goto retry;
    for (i = 1; i < NEW->toplevel; i++) {
        while (1) {
            pred = preds[i];
            succ = succs[i];
            /* Update the forward pointer if it is stale */
            new_next = NEW->nexts[i];
            if ((new_next != succ) &&
                (!ATOMIC_CAS_MB(&NEW->nexts[i], unset_mark((uintptr_t)new_next), succ)))
                break;
            /* Give up if pointer is marked */
            /* We retry the search if the CAS fails */
            if (ATOMIC_CAS_MB(&pred->nexts[i], succ, NEW))
                break;
            fraser_search(set, v, preds, succs);
        }
    }
}

#endif
