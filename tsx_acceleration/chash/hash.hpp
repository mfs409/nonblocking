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

class hashset_t
{
  private:

    struct hnode_t
    {
        atomic<hnode_t *>   old;
        atomic<int *>     * buckets;
        int                 size;
    };

    static hnode_t * alloc_hnode(hnode_t * o, int s)
    {
        hnode_t * t = (hnode_t *)wbmm_alloc(sizeof(hnode_t));
        t->old = o;
        t->size = s;
        t->buckets = (atomic<int *> *)wbmm_alloc(sizeof(atomic<int *>) * s);
        for (int i = 0; i < s; i++) t->buckets[i] = NULL;
        return t;
    }

    static void free_hnode_safe(hnode_t * t)
    {
        for (int i = 0; i < t->size; i++)
            wbmm_free_safe((void *)REF_UNMARKED(t->buckets[i].load()));
        wbmm_free_safe(t->buckets);
        wbmm_free_safe(t);
    }

    static void free_hnode_unsafe(hnode_t * t)
    {
        wbmm_free_unsafe(t->buckets);
        wbmm_free_unsafe(t);
    }

    static int * alloc_fset(int len)
    {
        int * arr = (int *)wbmm_alloc(sizeof(int) * (len + 1));
        arr[0] = len;
        return arr;
    }

    static void free_fset_safe(int * arr)
    {
        wbmm_free_safe(arr);
    }

    static void free_fset_unsafe(int * arr)
    {
        wbmm_free_unsafe(arr);
    }

  private:

    static const int MIN_BUCKET_NUM = 1;
    static const int MAX_BUCKET_NUM = 1 << 16;

    atomic<hnode_t *> head;

  public:

    hashset_t()
    {
        hnode_t * t = alloc_hnode(NULL, MIN_BUCKET_NUM);
        t->buckets[0] = alloc_fset(0);
        head = t;
    }

    bool insert(int key)
    {
        wbmm_begin();
        hnode_t * h = head;
        int result = apply(true, key);
        if (abs(result) > 2)
            resize(h, true);
        wbmm_end();
        return result > 0;
    }

    bool remove(int key)
    {
        wbmm_begin();
        int result = apply(false, key);
        wbmm_end();
        return result > 0;
    }

    bool contains(int key)
    {
        wbmm_begin();
        hnode_t * t = head;
        int     * b = t->buckets[key % t->size];
        // if the b is empty, use old table
        if (b == NULL) {
            hnode_t * s = t->old;
            b = (s == NULL)
                ? t->buckets[key % t->size]
                : s->buckets[key % s->size];
        }
        bool r = arrayContains((int *)REF_UNMARKED(b), key);
        wbmm_end();
        return r;
    }

    bool grow()
    {
        wbmm_begin();
        hnode_t * h = head;
        bool r = resize(h, true);
        wbmm_end();
        return r;
    }

    bool shrink()
    {
        wbmm_begin();
        hnode_t * h = head;
        bool r = resize(h, false);
        wbmm_end();
        return r;
    }

    string toString()
    {
        stringstream ss;
        hnode_t * curr = head;
        int age = 0;
        while (curr != NULL) {
            ss << "HashTableNode #" << age++ << endl;
            for (int i = 0; i < curr->size; i++) {
                ss << "  Bucket " << i << ": ";
                int * b = curr->buckets[i];
                if (IS_MARKED(b))
                    ss << "* ";
                if (b != NULL)
                    ss << bucketToString((int *)REF_UNMARKED(b));
                ss << endl;
            }
            curr = curr->old;
        }
        return ss.str();
    }


  private:

    int apply(bool insert, int key)
    {
        while (true) {
            hnode_t * t = head;
            int       i = key % t->size;
            int     * b = t->buckets[i];

            // if the b is empty, help finish resize
            if (b == NULL)
                helpResize(t, i);
            // otherwise enlist at b
            else {
                while (!IS_MARKED(b)) {
                    int * n = insert ? arrayInsert(b, key) : arrayRemove(b, key);
                    if (n == b)
                        return -(n[0] + 1);
                    else if (bcas(&t->buckets[i], &b, n)) {
                        free_fset_safe(b); // reclaim b
                        return n[0] + 1;
                    }
                    b = t->buckets[i];
                    free_fset_unsafe(n); // reclaim n
                }
            }
        }
    }

    bool resize(hnode_t * t, bool grow)
    {
        if ((t->size == MAX_BUCKET_NUM && grow) ||
            (t->size == MIN_BUCKET_NUM && !grow))
            return false;

        if (t == head) {
            // make sure we can deprecate t's predecessor
            for (int i = 0; i < t->size; i++) {
                if (t->buckets[i] == NULL)
                    helpResize(t, i);
            }
            // deprecate t's predecessor
            hnode_t * o = t->old;
            if (o && bcas(&(t->old), &o, (hnode_t *)NULL))
                free_hnode_safe(o);

            // switch to fresh bucket array
            if (t == head) {
                hnode_t * n = alloc_hnode(t, grow ? t->size * 2 : t->size / 2);
                if (!bcas(&head, &t, n)) {
                    free_hnode_unsafe(n); // free n
                }
                return true;
            }
        }
        return false;
    }

    void helpResize(hnode_t * t, int i)
    {
        int     * b = t->buckets[i];
        hnode_t * s = t->old;
        if (b == NULL && s != NULL) {
            int * set;
            if (s->size * 2 == t->size) /* growing */ {
                int * p = freezeBucket(s, i % s->size);
                set = split(p, t->size, i);
            }
            else /* shrinking */ {
                int * p = freezeBucket(s, i);
                int * q = freezeBucket(s, i + t->size);
                set = merge(p, q);
            }
            if (!bcas(&t->buckets[i], &b, set))
                free_fset_unsafe(set);
        }
    }

    string bucketToString(int * b)
    {
        stringstream ss;
        for (int i = 1; i <= b[0]; i++)
            ss << b[i] << " ";
        return ss.str();
    }

    int * split(int * o, int size, int remainder)
    {
        int count = 0;
        for (int i = 1; i <= o[0]; i++)
            if (o[i] % size == remainder)
                count++;
        int * n = alloc_fset(count);
        n[0] = count;
        int j = 1;
        for (int i = 1; i <= o[0]; i++) {
            if (o[i] % size == remainder)
                n[j++] = o[i];
        }
        return n;
    }

    int * merge(int * p, int * q)
    {
        int * n = alloc_fset(p[0] + q[0]);
        n[0] = p[0] + q[0];
        int j = 1;
        for (int i = 1; i <= p[0]; i++)
            n[j++] = p[i];
        for (int i = 1; i <= q[0]; i++)
            n[j++] = q[i];
        return n;
    }

    int * freezeBucket(hnode_t * t, int i)
    {
        while (true) {
            int * h = t->buckets[i];
            if (IS_MARKED(h))
                return (int *)REF_UNMARKED(h);
            if (bcas(&t->buckets[i], &h, (int *)REF_MARKED(h)))
                return h;
        }
    }

    bool arrayContains(int * o, int key)
    {
        for (int i = 1; i <= o[0]; i++) {
            if (o[i] == key)
                return true;
        }
        return false;
    }

    int * arrayInsert(int * o, int key)
    {
        if (arrayContains(o, key))
            return o;
        int * n = alloc_fset(o[0] + 1);
        for (int i = 1; i <= o[0]; i++)
            n[i] = o[i];
        n[n[0]] = key;
        return n;
    }

    int * arrayRemove(int * o, int key)
    {
        if (!arrayContains(o, key))
            return o;
        int * n = alloc_fset(o[0] - 1);
        int j = 1;
        for (int i = 1; i <= o[0]; i++) {
            if (o[i] != key)
                n[j++] = o[i];
        }
        return n;
    }
};
