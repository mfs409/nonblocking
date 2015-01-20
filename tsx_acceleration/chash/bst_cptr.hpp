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

class bstset_cptr_t
{
  private:
    static const int MAX_ATTEMPT_NUM = 4;

  private:
    struct bstnode_t
    {
        int32_t             key;
        atomic<bstnode_t *> left;
        atomic<bstnode_t *> right;
        atomic<uint64_t>    info;
    };

    enum infotype_t {
        DINFO,
        IINFO,
        MARK,
        CLEAN
    };

    struct dinfo_t
    {
        infotype_t   type;
        bstnode_t *  l;
        bstnode_t *  p;
        bstnode_t *  gp;
        cptr_t<void> pinfo;
    };

    struct iinfo_t
    {
        infotype_t  type;
        bstnode_t * l;
        bstnode_t * p;
        bstnode_t * newInternal;
    };

    struct mark_t
    {
        infotype_t   type;
        cptr_t<void> dinfo;
    };

    static inline infotype_t GET_INFO_TYPE(void * ptr) {
        return ptr ? (*(infotype_t *)(ptr)) : CLEAN;
    }

    static inline bool INFO_IS_CLEAN(void * ptr) {
        return ((ptr) == NULL);
    }

  private:
    static const int32_t INF = std::numeric_limits<int32_t>::max();

    bstnode_t * root;

    /** Allocate a leaf node. */
    static bstnode_t * alloc_bstnode(int32_t key)
    {
        bstnode_t * l = (bstnode_t *)wbmm_alloc(sizeof(bstnode_t));
        l->key = key;
        l->left = NULL;
        l->right = NULL;
        l->info = 0;
        return l;
    }

    /** Allocate an internal node. */
    static bstnode_t * alloc_bstnode(int32_t key, bstnode_t * left, bstnode_t * right)
    {
        bstnode_t * i = (bstnode_t *)wbmm_alloc(sizeof(bstnode_t));
        i->key = key;
        i->left = left;
        i->right = right;
        i->info = 0;
        return i;
    }

    static iinfo_t * alloc_iinfo(bstnode_t * l, bstnode_t * p, bstnode_t * newInternal)
    {
        iinfo_t * i = (iinfo_t *)wbmm_alloc(sizeof(iinfo_t));
        *i = {IINFO, l, p, newInternal};
        return i;
    }

    static dinfo_t * alloc_dinfo(bstnode_t * l, bstnode_t * p, bstnode_t * gp, cptr_t<void> pinfo)
    {
        dinfo_t * d = (dinfo_t *)wbmm_alloc(sizeof(dinfo_t));
        *d = {DINFO, l, p, gp, pinfo};
        return d;
    }

    static mark_t * alloc_mark(cptr_t<void> dinfo)
    {
        mark_t * m = (mark_t *)wbmm_alloc(sizeof(mark_t));
        *m = {MARK, dinfo};
        return m;
    }

    static void free_bstnode_safe(bstnode_t * n)
    {
        wbmm_free_safe(n);
    }

    static void free_bstnode_unsafe(bstnode_t * n)
    {
        // assumes info == null
        wbmm_free_unsafe(n);
    }

    static void free_info_safe(void * info)
    {
        wbmm_free_safe(info);
    }

    static void free_info_unsafe(void * info)
    {
        wbmm_free_unsafe(info);
    }

  public:
    bstset_cptr_t()
    {
        // make sure pointer size is 32bit since we are using 64bit counted pointers
        assert(sizeof(uintptr_t) == sizeof(uint32_t));
        assert(sizeof(cptr_t<int>) == sizeof(uint64_t));
        bstnode_t * l1 = alloc_bstnode(INF);
        bstnode_t * l2 = alloc_bstnode(INF);
        root = alloc_bstnode(INF, l1, l2);
    }

    bool contains(int key)
    {
        wbmm_begin();
        bstnode_t * l = root->left;
        while (l->left != NULL) {
            l = (key < l->key) ? l->left : l->right;
        }
        bool result = key == l->key;
        wbmm_end();
        return result;
    }

    bool insert(int key)
    {
        wbmm_begin();

        bstnode_t * newNode = alloc_bstnode(key);
        bstnode_t * newSibling;
        bstnode_t * newInternal;
        bstnode_t * p;
        bstnode_t * l;
        cptr_t<void> pinfo;
        bool result;

        while (true) {
            /** SEARCH **/
            p = root;
            pinfo.all = p->info;
            l = p->left;
            while (l->left != NULL) {
                p = l;
                l = (key < l->key) ? l->left : l->right;
            }
            pinfo.all = p->info;
            if (l != p->left && l != p->right)
                continue;
            /** END SEARCH **/

            if (key == l->key) {
                result = false;
                break;
            }
            else if (!INFO_IS_CLEAN(pinfo.fields.ptr))
                help(pinfo);
            else {
                newSibling = alloc_bstnode(l->key);
                newInternal = (key < l->key)
                    ? alloc_bstnode(l->key, newNode, newSibling)
                    : alloc_bstnode(key, newSibling, newNode);

                void * newPInfo = alloc_iinfo(l, p, newInternal);
                cptr_t<void> nw;
                MAKE_CPTR(nw, newPInfo, pinfo.fields.ctr + 1);

                // try to IFlag parent
                if (bcas(&p->info, &pinfo.all, nw.all)) {
                    free_info_safe(pinfo.fields.ptr); // reclaim old info object
                    helpInsert(nw);
                    result = true;
                    break;
                }
                else {
                    // free local objects
                    free_info_unsafe(newPInfo);
                    free_bstnode_unsafe(newSibling);
                    free_bstnode_unsafe(newInternal);
                    help(pinfo);
                }
            }
        }

        wbmm_end();
        return result;
    }

    bool remove(int key)
    {
        wbmm_begin();

        bstnode_t * gp;
        bstnode_t * p;
        bstnode_t * l;
        bool result;

        cptr_t<void> gpinfo;
        cptr_t<void> pinfo;

        while (true) {
            /** SEARCH **/
            gp = NULL;
            gpinfo.all = 0;
            p = root;
            pinfo.all = p->info;
            l = p->left;
            while (l->left != NULL) {
                gp = p;
                p = l;
                l = (key < l->key) ? l->left : l->right;
            }

            if (gp != NULL) {
                gpinfo.all = gp->info;
                if (p != gp->left && p != gp->right)
                    continue;
                pinfo.all = p->info;
                if (l != p->left && l != p->right)
                    continue;
            }
            /** END SEARCH **/

            if (key != l->key) {
                result = false;
                break;
            }
            else if (!INFO_IS_CLEAN(gpinfo.fields.ptr))
                help(gpinfo);
            else if (!INFO_IS_CLEAN(pinfo.fields.ptr))
                help(pinfo);
            else {
                // try to DFlag grandparent
                void * newGPInfo = alloc_dinfo(l, p, gp, pinfo);
                cptr_t<void> nw;
                MAKE_CPTR(nw, newGPInfo, gpinfo.fields.ctr + 1);

                if (bcas(&gp->info, &gpinfo.all, nw.all)) {
                    free_info_safe(gpinfo.fields.ptr); // free old info object
                    if (helpDelete(nw)) {
                        result = true;
                        break;
                    }
                }
                else {
                    free_info_unsafe(newGPInfo); // free local object
                    help(gpinfo);
                }
            }
        }

        wbmm_end();
        return result;
    }

    bool grow() { return false; }
    bool shrink() { return false; }

  private:

    void help(cptr_t<void> w)
    {
        void * info = w.fields.ptr;
        if (GET_INFO_TYPE(info) == IINFO)
            helpInsert(w);
        else if (GET_INFO_TYPE(info) == DINFO)
            helpDelete(w);
        else if (GET_INFO_TYPE(info) == MARK)
            helpMarked(((mark_t *)info)->dinfo);
    }

    void helpInsert(cptr_t<void> w)
    {
        iinfo_t * info = (iinfo_t *)w.fields.ptr;

        bstnode_t * l = info->l;
        bstnode_t * p = info->p;
        atomic<bstnode_t *> * ptr = (p->left == l) ? &p->left : &p->right;
        if (bcas(ptr, &l, info->newInternal))
            free_bstnode_safe(l); // assert(l->info == NULL)

        cptr_t<void> nw;
        MAKE_CPTR(nw, NULL, w.fields.ctr + 1);

        if (bcas(&p->info, &w.all, nw.all))
            free_info_safe(info);
    }

    bool helpDelete(cptr_t<void> w)
    {
        dinfo_t * info = (dinfo_t *)w.fields.ptr;

        bstnode_t * p = info->p;
        bstnode_t * gp = info->gp;
        cptr_t<void> pinfo = info->pinfo;

        void * m = alloc_mark(w);
        cptr_t<void> marker;
        MAKE_CPTR(marker, m, pinfo.fields.ctr + 1);

        if (bcas(&p->info, &pinfo.all, marker.all)) {
            free_info_safe(pinfo.fields.ptr); // reclaim old pinfo
            helpMarked(w);
            return true;
        }
        else {
            free_info_unsafe(m); // free local object
            pinfo.all = p->info;
            void * currentPInfo = pinfo.fields.ptr;
            if (GET_INFO_TYPE(currentPInfo) == MARK
                && ((mark_t *)currentPInfo)->dinfo.all == w.all) {
                helpMarked(w);
                return true;
            }
            else {
                help(pinfo);

                cptr_t<void> nw;
                MAKE_CPTR(nw, NULL, w.fields.ctr + 1);

                if (bcas(&gp->info, &w.all, nw.all))
                    free_info_safe(info); // reclaim old gpinfo
                return false;
            }
        }
    }

    void helpMarked(cptr_t<void> w)
    {
        dinfo_t * info = (dinfo_t *)w.fields.ptr;

        bstnode_t * l = info->l;
        bstnode_t * p = info->p;
        bstnode_t * gp = info->gp;

        bstnode_t * other = (p->right == l) ? p->left : p->right;
        atomic<bstnode_t *> * ptr = (gp->left == p) ? &gp->left : &gp->right;
        if (bcas(ptr, &p, other)) {
            cptr_t<void> pinfo;
            pinfo.all = p->info;
            free_info_safe(pinfo.fields.ptr); // reclaim pinfo
            free_bstnode_safe(l); // reclaim l (l->info == NULL)
            free_bstnode_safe(p); // reclaim p
        }

        cptr_t<void> nw;
        MAKE_CPTR(nw, NULL, w.fields.ctr + 1);

        if (bcas(&gp->info, &w.all, nw.all))
            free_info_safe(info); // reclaim old gpinfo
    }
};
