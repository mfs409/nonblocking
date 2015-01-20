#pragma once

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <atomic>
#include <x86intrin.h>

#include "common.hpp"
#include "mm.hpp"

using std::atomic;
using std::stringstream;
using std::string;
using std::endl;

class hashset_htm_t
{
  private:

    static const int MIN_BUCKET_NUM = 1;
    static const int MAX_BUCKET_NUM = 1 << 16;

    static const int MAX_ATTEMPT_NUM = 1;
    static const int MIN_ALLOC_LEN = 4;

  private:

    struct hnode_t
    {
        atomic<hnode_t *>  old;
        atomic<uint64_t> * buckets;
        int                size;
    };

    static hnode_t * alloc_hnode(hnode_t * o, int s)
    {
        hnode_t * t = (hnode_t *)wbmm_alloc(sizeof(hnode_t));
        t->old = o;
        t->size = s;
        t->buckets = (atomic<uint64_t> *)wbmm_alloc(sizeof(atomic<uint64_t>) * s);
        for (int i = 0; i < s; i++) t->buckets[i] = 0;
        return t;
    }

    static void free_hnode_safe(hnode_t * t)
    {
        for (int i = 0; i < t->size; i++) {
            cptr_t<int> p;
            p.all = t->buckets[i];
            wbmm_free_safe((void *)REF_UNMARKED(p.fields.ptr));
        }
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
        int alloc_len = std::max(MIN_ALLOC_LEN + 1, len + 1);
        int * arr = (int *)wbmm_alloc(sizeof(int) * alloc_len);
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

    atomic<hnode_t *> head;

  public:

    hashset_htm_t()
    {
        // make sure pointer size is 32bit since we are using 64bit counted pointers
        assert(sizeof(uintptr_t) == sizeof(uint32_t));
        assert(sizeof(cptr_t<int>) == sizeof(uint64_t));
        hnode_t * t = alloc_hnode(NULL, MIN_BUCKET_NUM);
        int * b = alloc_fset(0);
        cptr_t<int> w;
        MAKE_CPTR(w, b, 0);
        t->buckets[0] = w.all;
        head = t;
    }

    bool insert(int key)
    {
        hnode_t * t;
        int result;

        uint32_t status;
        uint32_t attempts = 0;
      retry:
        status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            t = head;
            int i = key % t->size;
            int     * b;
            cptr_t<int> w;
            w.all = t->buckets[i];
            b = w.fields.ptr;

            if (b == NULL || IS_MARKED(b))
                _xabort(42);

            int * n = arrayInsert(b, key);
            if (n == b)
                result = -(n[0] + 1);
            else {
                w.fields.ptr = n;
                w.fields.ctr++;
                t->buckets[i] = w.all;
                free_fset_safe(b); // reclaim b
                result = n[0] + 1;
            }
            _xend(); // commit fast path
            wbmm_begin();
            if (abs(result) > 2)
                resize(t, true);
            wbmm_end();
            return result > 0;
        }
        else {
            if ((status & _XABORT_EXPLICIT) && _XABORT_CODE(status) == 42) {
                // try slow path
            }
            else if (++attempts < MAX_ATTEMPT_NUM) {
                goto retry;
            }
        }

        wbmm_begin();
        t = head;
        result = apply(true, key);
        if (abs(result) > 2)
            resize(t, true);
        wbmm_end();
        return result > 0;
    }

    bool remove(int key)
    {
        int result;
        uint32_t status;
        uint32_t attempts = 0;
      retry:
        status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            hnode_t * t = head;
            int i = key % t->size;
            int     * b;
            cptr_t<int> w;
            w.all = t->buckets[i];
            b = w.fields.ptr;

            if (b == NULL || IS_MARKED(b))
                _xabort(42);

            int * n = arrayRemove(b, key);
            if (n == b)
                result = -(n[0] + 1);
            else {
                w.fields.ptr = n;
                w.fields.ctr++;
                t->buckets[i] = w.all;
                free_fset_safe(b); // reclaim b
                result = n[0] + 1;
            }
            _xend(); // commit fast path
            return result > 0;
        }
        else {
            if ((status & _XABORT_EXPLICIT) && _XABORT_CODE(status) == 42) {
                // try slow path
            }
            else if (++attempts < MAX_ATTEMPT_NUM) {
                goto retry;
            }
        }

        wbmm_begin();
        result = apply(false, key);
        wbmm_end();
        return result > 0;
    }

    bool contains(int key)
    {
        uint32_t status;
        uint32_t attempts = 0;
      retry:
        status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            hnode_t * t = head;
            cptr_t<int> w;
            w.all = t->buckets[key % t->size];
            int * b = w.fields.ptr;
            if (b == NULL) {
                hnode_t * s = t->old;
                w.all = s->buckets[key % s->size];
                b = w.fields.ptr;
            }
            bool r = arrayContains((int *)REF_UNMARKED(b), key);
            _xend();
            return r;
        }
        else {
            if ((status & _XABORT_EXPLICIT) && _XABORT_CODE(status) == 42) {
                // try slow path
            }
            else if (++attempts < MAX_ATTEMPT_NUM) {
                goto retry;
            }
        }

        wbmm_begin();

        hnode_t * t = head;
        cptr_t<int> w;
        w.all = t->buckets[key % t->size];
        int     * b = w.fields.ptr;
        // if the b is empty, use old table
        if (b == NULL) {
            hnode_t * s = t->old;
            w.all = (s == NULL)
                ? t->buckets[key % t->size]
                : s->buckets[key % s->size];
            b = w.fields.ptr;
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

                cptr_t<int> w;
                w.all = curr->buckets[i];
                int * b = w.fields.ptr;

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

            int     * b;
            cptr_t<int> w;
            w.all = t->buckets[i];
            b = w.fields.ptr;

            // if the b is empty, help finish resize
            if (b == NULL)
                helpResize(t, i);
            // otherwise enlist at b
            else {
                while (!IS_MARKED(b)) {
                    int * n = insert ? arrayInsert(b, key) : arrayRemove(b, key);
                    if (n == b)
                        return -(n[0] + 1);
                    else {
                        cptr_t<int> nw;
                        MAKE_CPTR(nw, n, w.fields.ctr + 1);
                        if (bcas(&t->buckets[i], &w.all, nw.all)) {
                            free_fset_safe(b); // reclaim b
                            return n[0] + 1;
                        }
                    }
                    w.all = t->buckets[i];
                    b = w.fields.ptr;
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
                cptr_t<int> w;
                w.all = t->buckets[i];
                if (w.fields.ptr == NULL)
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
        int     * b;
        cptr_t<int> w;
        w.all = t->buckets[i];
        b = w.fields.ptr;

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
            cptr_t<int> nw;
            MAKE_CPTR(nw, set, w.fields.ctr + 1);
            if (!bcas(&t->buckets[i], &w.all, nw.all))
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
            int     * b;
            cptr_t<int> w;
            w.all = t->buckets[i];
            b = w.fields.ptr;

            if (IS_MARKED(b))
                return (int *)REF_UNMARKED(b);

            cptr_t<int> nw;
            MAKE_CPTR(nw, (int *)REF_MARKED(b), w.fields.ctr + 1);

            if (bcas(&t->buckets[i], &w.all, nw.all))
                return b;
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
