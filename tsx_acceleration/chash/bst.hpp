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

class bstset_t
{
    struct bstnode_t
    {
        int32_t             key;
        atomic<bstnode_t *> left;
        atomic<bstnode_t *> right;
        atomic<void *>      info;
    };

    enum infotype_t {
        DINFO,
        IINFO,
        MARK,
        CLEAN
    };

    struct dinfo_t
    {
        infotype_t  type;
        bstnode_t * l;
        bstnode_t * p;
        bstnode_t * gp;
        void *      pinfo;
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
        infotype_t  type;
        dinfo_t *   dinfo;
    };

    struct clean_t
    {
        infotype_t  type;
    };

    static inline infotype_t GET_INFO_TYPE(void * ptr) {
        return (*(infotype_t *)(ptr));
    }

    static inline bool INFO_IS_CLEAN(void * ptr) {
        return ((ptr) == NULL || GET_INFO_TYPE(ptr) == CLEAN);
    }

    /** Allocate a leaf node. */
    static bstnode_t * alloc_bstnode(int32_t key)
    {
        bstnode_t * l = (bstnode_t *)wbmm_alloc(sizeof(bstnode_t));
        l->key = key;
        l->left = NULL;
        l->right = NULL;
        l->info = NULL;
        return l;
    }

    /** Allocate an internal node. */
    static bstnode_t * alloc_bstnode(int32_t key, bstnode_t * left, bstnode_t * right)
    {
        bstnode_t * i = (bstnode_t *)wbmm_alloc(sizeof(bstnode_t));
        i->key = key;
        i->left = left;
        i->right = right;
        i->info = NULL;
        return i;
    }

    static iinfo_t * alloc_iinfo(bstnode_t * l, bstnode_t * p, bstnode_t * newInternal)
    {
        iinfo_t * i = (iinfo_t *)wbmm_alloc(sizeof(iinfo_t));
        *i = {IINFO, l, p, newInternal};
        return i;
    }

    static dinfo_t * alloc_dinfo(bstnode_t * l, bstnode_t * p, bstnode_t * gp, void * pinfo)
    {
        dinfo_t * d = (dinfo_t *)wbmm_alloc(sizeof(dinfo_t));
        *d = {DINFO, l, p, gp, pinfo};
        return d;
    }

    static mark_t * alloc_mark(dinfo_t * dinfo)
    {
        mark_t * m = (mark_t *)wbmm_alloc(sizeof(mark_t));
        *m = {MARK, dinfo};
        return m;
    }

    static clean_t * alloc_clean()
    {
        clean_t * c = (clean_t *)wbmm_alloc(sizeof(clean_t));
        *c = {CLEAN};
        return c;
    }

    static void free_bstnode_safe(bstnode_t * n)
    {
        if (n->info) wbmm_free_safe(n->info);
        wbmm_free_safe(n);
    }

    static void free_bstnode_unsafe(bstnode_t * n)
    {
        if (n->info) wbmm_free_unsafe(n->info);
        wbmm_free_unsafe(n);
    }

    static void free_info_safe(void * info)
    {
        if (info) wbmm_free_safe(info);
    }

    static void free_info_unsafe(void * info)
    {
        if (info) wbmm_free_unsafe(info);
    }

  private:

    static const int32_t INF = std::numeric_limits<int32_t>::max();

    bstnode_t * root;

  public:
    bstset_t()
    {
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
        void * pinfo;
        bool result;

        while (true) {
            /** SEARCH **/
            p = root;
            pinfo = p->info;
            l = p->left;
            while (l->left != NULL) {
                p = l;
                l = (key < l->key) ? l->left : l->right;
            }
            pinfo = p->info;
            if (l != p->left && l != p->right)
                continue;
            /** END SEARCH **/

            if (key == l->key) {
                result = false;
                break;
            }
            else if (!INFO_IS_CLEAN(pinfo))
                help(pinfo);
            else {
                newSibling = alloc_bstnode(l->key);
                newInternal = (key < l->key)
                    ? alloc_bstnode(l->key, newNode, newSibling)
                    : alloc_bstnode(key, newSibling, newNode);

                void * newPInfo = alloc_iinfo(l, p, newInternal);

                // try to IFlag parent
                if (bcas(&p->info, &pinfo, newPInfo)) {
                    free_info_safe(pinfo); // reclaim old info object
                    helpInsert((iinfo_t *)newPInfo);
                    result = true;
                    break;
                }
                else {
                    // free local objects
                    free_info_unsafe(newPInfo);
                    free_bstnode_unsafe(newSibling);
                    free_bstnode_unsafe(newInternal);
                    help(p->info);
                }
            }
        }

        wbmm_end();
        return result;
    }

    bool remove(int key)
    {
        wbmm_begin();

        void * gpinfo;
        void * pinfo;
        bstnode_t * gp;
        bstnode_t * p;
        bstnode_t * l;
        bool result;

        while (true) {
            /** SEARCH **/
            gp = NULL;
            gpinfo = NULL;
            p = root;
            pinfo = p->info;
            l = p->left;
            while (l->left != NULL) {
                gp = p;
                p = l;
                l = (key < l->key) ? l->left : l->right;
            }

            if (gp != NULL) {
                gpinfo = gp->info;
                if (p != gp->left && p != gp->right)
                    continue;
                pinfo = p->info;
                if (l != p->left && l != p->right)
                    continue;
            }
            /** END SEARCH **/

            if (key != l->key) {
                result = false;
                break;
            }
            else if (!INFO_IS_CLEAN(gpinfo))
                help(gpinfo);
            else if (!INFO_IS_CLEAN(pinfo))
                help(pinfo);
            else {
                // try to DFlag grandparent
                void * newGPInfo = alloc_dinfo(l, p, gp, pinfo);
                void * oldGPInfo = gpinfo;
                if (bcas(&gp->info, &oldGPInfo, newGPInfo)) {
                    free_info_safe(oldGPInfo); // free old info object
                    if (helpDelete((dinfo_t *)newGPInfo)) {
                        result = true;
                        break;
                    }
                }
                else {
                    free_info_unsafe(newGPInfo); // free local object
                    help(gp->info);
                }
            }
        }

        wbmm_end();
        return result;
    }

    bool grow() { return false; }
    bool shrink() { return false; }

  private:

    void help(void * info)
    {
        if (GET_INFO_TYPE(info) == IINFO)
            helpInsert((iinfo_t *)info);
        else if (GET_INFO_TYPE(info) == DINFO)
            helpDelete((dinfo_t *)info);
        else if (GET_INFO_TYPE(info) == MARK)
            helpMarked(((mark_t *)info)->dinfo);
    }

    void helpInsert(iinfo_t * info)
    {
        bstnode_t * l = info->l;
        bstnode_t * p = info->p;
        atomic<bstnode_t *> * ptr = (p->left == l) ? &p->left : &p->right;
        if (bcas(ptr, &l, info->newInternal))
            free_bstnode_safe(l);
        void * c = alloc_clean();
        if (bcas(&p->info, (void **)&info, c))
            free_info_safe(info);
        else
            free_info_unsafe(c);
    }

    bool helpDelete(dinfo_t * info)
    {
        bstnode_t * p = info->p;
        bstnode_t * gp = info->gp;
        void * pinfo = info->pinfo;

        void * m = alloc_mark(info);

        if (bcas(&p->info, &pinfo, m)) {
            free_info_safe(pinfo); // reclaim old pinfo
            helpMarked(info);
            return true;
        }
        else {
            free_info_unsafe(m); // free local object
            void * currentPInfo = info->p->info;
            if (GET_INFO_TYPE(currentPInfo) == MARK && ((mark_t *)currentPInfo)->dinfo == info) {
                helpMarked(info);
                return true;
            }
            else {
                help(currentPInfo);
                void * c = alloc_clean();
                if (bcas(&gp->info, (void **)&info, c))
                    free_info_safe(info); // reclaim old gpinfo
                else
                    free_info_unsafe(c); // free local object
                return false;
            }
        }
    }

    void helpMarked(dinfo_t * info)
    {
        bstnode_t * l = info->l;
        bstnode_t * p = info->p;
        bstnode_t * gp = info->gp;

        bstnode_t * other = (p->right == l) ? p->left : p->right;
        atomic<bstnode_t *> * ptr = (gp->left == p) ? &gp->left : &gp->right;
        if (bcas(ptr, &p, other)) {
            free_bstnode_safe(l); // reclaim l
            free_bstnode_safe(p); // reclaim p
        }
        void * c = alloc_clean();
        if (bcas(&gp->info, (void **)&info, c))
            free_info_safe(info); // reclaim old gpinfo
        else
            free_info_unsafe(c); // free local object
    }
};
