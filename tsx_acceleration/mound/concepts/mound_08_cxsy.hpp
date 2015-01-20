/**
 *  Mound Refinement 8: Replace atomic blocks with while loops that exit
 *  based on a CompareXSwapY instruction.  Note that cxsy is more useful than
 *  MCAS, because we don't 'modify' nodes when keeping them constant.
 */

/**
 * Simple linked-list holding integers... our payload is an integer, but
 * could be any word-sized datum
 */
struct List
{
    const int   data;
    const List* next;
};

/**
 * Nodes in the Mound consist of a pointer to a list, a flag indicating if
 * the list is in an unusable state, and a timestamp.
 */
struct Node
{
  private:
    volatile List* list   = null;            // pointer to head node
    volatile bool  cavity = false;           // indicate whether the node is a cavity
    volatile int   ts     = 0;               // timestamp... useful for breaking up
                                             // atomic sections
  public:
    nodecache_t READ();  // returns a nodecache
    WRITE(nodecache_t n); // takes a nodecache
};

/**
 *  This is to capture the fact that we can read a Node to a nodecache and then access the fields one at a time
 */
struct nodecache_t
{
    List* list   = null;
    bool  cavity = false;
    int   ts     = 0;
};


///////////////////////////////////////////////////////
//
// INSERT CODE
//
///////////////////////////////////////////////////////

/**
 *  Insertion into the mound.
 */
void insert_cxsy(int n)
{
    // pick an initial Leaf.  Note that there is some atomicity needed in the
    // function, though we don't provide the function (yet).
    Node* L = select_random_leaf();
    while (true) {
        nodecache_t LL;
        while (true) {
            LL = L->READ();
            // make sure Leaf not cavity
            if (LL->is_cavity) throw LeafIsCavity; // SAFE! single atomic read
            // make sure Leaf data >= n

            // if leaf's list is null, we're good
            if (LL.list == NULL) break; // SAFE! single atomic read

            // if leaf's list not null, we need to be a little more careful,
            // due to the possibility for LL.data to be recycled during
            // execution
            int x = LL.list->data;
            nodecache_t TT = L->READ();
            if (TT.ts != LL.ts)
                continue; // atomic snapshot failed
            if (x < n) throw LeafTooBig; // leaf too big, and read of list valid
            break; // leaf OK, and read of list valid
        }
        catch (LeafIsCavity) {
            fill_cavity_cxsy(L); continue;
        }
        catch (LeafTooBig) {
            L = select_random_leaf(); continue;
        }
        // if we made it here, then we have a leaf with value that is >= n
        // begin traversing up the ancestor chain, until we either reach the
        // root, or find a parent/child pair where the parent value is < n
        // and the child value is >=N
        Node *C = L, *P = NULL;
        nodecache_t CC = LL, PP;
        while (true) {
            // [mfs] We could be much more optimitic here.  What if we
            //       started with P?  If P is == n, we don't care about C
            //       because we are going to insert at P.  If P is > n, we
            //       don't care about C because we are going to traverse up.
            //       If P < n, then we can make sure C <= n and insert at C.
            //       If the test of C fails, then we need to restart.
            //
            // [mfs] For that matter, do we care about cavities?  Only if we
            //       want to insert at a location that is cavity?
            //
            // [mfs] I think we can really be aggressive about cavities.  No
            //       insert should ever need to fill one.
            //
            //       where can we insert:
            //
            //         - at the root.  If I shrink the root while it's
            //           cavity, I just increase the chance that a future
            //           cavity fill will only need a CAS on the root.
            //
            //         - at an equal node.  If I do this, it doesn't matter
            //           if the node is a cavity.
            //
            //         - at a child > val with parent < val.  If parent is
            //           cavity, then it doesn't matter that I'm doing this,
            //           because the parent fill won't use my value anyway.
            //           If the child is cavity, then the same logic applies
            //           as at root.
            while (true) {
                // make sure the node passed from previous transaction is
                // still valid
                nodecache_t TT = C->READ();
                if (CC.ts != TT.ts) throw ChildChanged; // SAFE! single atomic read
                // if n == C's value, we can insert locally and be done
                if ((CC.list != NULL) && (CC.list->data == n)) {
                    if (ATOMIC_CAS(C, CC, (new List(n, CC->list), false, CC.ts+1)))
                        return;
                    continue; // actually should throw ChildChanged?
                }

                // need to see what's up with parent
                P = parentof(C);
                PP = P->READ();
                // NB: if C changed, then maybe P isn't a cavity anymore...
                //     Better atomicity could help here.
                if (PP.is_cavity) throw ParentCavity; // SAFE to release C

                // can we insert at C?
                //
                // NB: again, possible atomicity bug here if PP changes?  If
                // we drop 'atomic' blocks, do we need to validate PP after
                // reading PP->list->data before doing the write?
                if ((PP.list != NULL) && (PP.list->data < n)) {
                    if (ATOMIC_C2S1(C, CC, (new List(n, CC->list), false, CC.ts+1),
                             P, PP, NO_CHANGE))
                        return;
                    continue;
                }

                // is P root?  If so, insert at P.  SAFE to release C
                if (is_root(P)) {
                    if (ATOMIC_CAS(P, PP, (new List(n, PP.list), false, PP.ts+1)))
                        return;
                    continue;
                }
                // need to move up one level
                P = C;
                PP = CC;
                // NB: we can release C, so now this reduces to a single
                //     location atomic read
                break;
            }
            catch (ChildChanged) {
                // this is tricky.  We really don't want to have to quit the
                // entire outer while loop.

                // We can end up here if C == L and C changed since the prior
                // atomic block... can we recover?  If C not cavity and C's
                // value still >= n, then probably... TODO

                // We can end up here with non-leaf C, too.  In that case, I
                // think the same holds: if C's value still >= n, we're still
                // OK

                // worst case: break to end this while loop, wrap around, and
                // start over from L.
                break;
            }
            catch (ParentCavity) {
                // P is a cavity... what should we do?
                fill_cavity_cxsy(P);
                continue;  // 33% chance that we changed cts.  Often, the
                           // change to cts is benign.  Can we work around?
                           // TODO

                // I think the optimization here is that we say
                //
                // if P >= n and not P.cavity, make C = P, update CC to PP,
                // and then continue.
                //
                // Else if C >= n and not C.cavity, update CC and move on.
                // Else break!
            }
        }
    }
}

///////////////////////////////////////////////////////
//
// FILL_CAVITY CODE
//
///////////////////////////////////////////////////////

/**
 *  Now this is its own transaction
 *
 *  Fills a cavity, possibly with recursion
 *
 *  NB: there is no atomicity between caller and this, so we must be sure
 *      that N still is a cavity
 */
void fill_cavity_cxsy(Node* N)
{
    // note that we do not support reducing the size of a mound, so if it
    // isn't a leaf, we don't need atomicity between the check and subsequent
    // ops.  Furthermore, the check can be outside the /while/, because this
    // isn't going to become a leaf.  However, this could stop being a leaf,
    // so the check needs to be atomic wrt expanding the mound.
    while (true) {
        if (is_leaf(N)) {
            nodecache_t NN = N->READ();
            if (NN.is_cavity) {
                if (!ATOMIC_C2S1(N, NN, (NN.list, false, NN.ts + 1),
                         LEAFINFO, LEAFINFO, NO_CHANGE))
                    continue;
            }
            return;
        }
        break;
    }

    // Now comes the hard work.
    while (true) {
        Node* R = rightof(N);
        Node* L = leftof(N);
        // for caching timestamps etc
        nodecache_t NN, RR, LL;
        // values
        int nv, rv, lv;
        // If N isn't a cavity, we're done.  Otherwise, to fill the cavity at
        // N, we must first be sure that N's children are both not cavities,
        // and we need to know the values in the heads of the lists at N and
        // N's children.  This transaction + compensation can only be passed
        // when all of the above are achieved
        //
        // [mfs] We probably don't need to read the lists inside of the
        //       transaction, since they are immutable
        while (true) {
            // N must be a cavity
            NN = N->READ();
            if (!NN.is_cavity) return; // SAFE: single atomic read
            nv = (NN.list == NULL) ? top : NN.list->data;
            // right can't be a cavity
            RR = R->READ();
            if (RR.is_cavity)
                throw RightCavity;    // SAFE to release N
            rv = (RR.list == NULL) ? top : RR.list->data;
            // left can't be a cavity
            LL = L->READ();
            if (LL.is_cavity)
                throw LeftCavity;     // SAFE to release N, R
            lv = (LL.list == NULL) ? top : LL.list->data;
            // not strictly necessary, since we validate the appropriate
            // portion in following work
            if (ATOMIC_C3S0(N,NN, R,RR, L,LL))
                break;
            continue;
        }
        catch (RightCavity) {
            fill_cavity_cxsy(N->right); continue;
        }
        catch (LeftCavity) {
            fill_cavity_cxsy(N->left); continue;
        }

        // NN, RR, LL all valid; nv, rv, lv all valid.  We can easily
        // decide what to do

        // If n is smallest, but someone is inserting at r, then either the
        // insert is larger than n (equal to r), and we don't care, or else
        // the insert will need to look at N.ts.  Thus simple cavity clears
        // can be atomic just on N

        // Likewise, if l is smallest but someone is inserting at r, same
        // logic applies.  So these atomic blocks need to look at a max of
        // two nodes

        // pull from right?
        if ((rv <= lv) && (rv < nv)) {
            // [mfs] don't really need a while loop here
            while (true) {
                // make sure N and R unchanged
                nodecache_t TT = N->READ();
                if (NN.ts != TT.ts) break;
                TT = R->READ();
                if (RR.ts != TT.ts) break;

                // move R's list head into N.  Note that we can't swap, but
                // instead copy the node
                List* x = RR.list;
                List* new_x = new List(x->data, NN.list);
                if (ATOMIC_C2S2(N, NN, (new_x, false, NN.ts + 1),
                         R, RR, (x->next, true, RR.ts + 1)))
                {
                    free(x);
                    return;
                }
                continue; // why?  we're going to fail in a N/R check...
            }
            continue;
        }
        // pull from left?
        if ((lv <= rv) && (lv < nv)) {
            while (true) {
                // make sure N and L unchanged
                nodecache_t TT = N->READ();
                if (NN.ts != TT.ts) break;
                TT = L->READ();
                if (LL.ts != TT.ts) break;

                // move L's list head into N.  Note that we can't swap, but
                // instead copy the node
                List* x = LL.list;
                List* new_x = new List(x->data, NN.list);
                if (ATOMIC_C2S2(N, NN, (new_x, false, NN.ts + 1),
                         L, LL, (x->next, true, LL.ts + 1)))
                {
                    free(x);
                    return;
                }
                continue; // again, why?
            }
            continue;
        }
        // pull from local list?
        while (true) {
            // make sure N unchanged
            nodecache_t TT = N->READ();
            if (NN.ts != TT.ts) break;

            // just clear the cavity and we're good
            if (ATOMIC_CAS(N, NN, (NN.list, false, NN.ts + 1)))
                return;
            continue;
        }
    }
}

///////////////////////////////////////////////////////
//
// EXTRACT_MIN CODE
//
///////////////////////////////////////////////////////

/**
 *  atomic extract-min with non-composed call to fill_cavity
 */
int extract_min_cxsy()
{
    List* extracted = NULL;
    while (true) {
        while (true) {
            nodecache_t RR = ROOT->READ();
            if (RR.iscavity)
                throw RootCavity; // SAFE: single read
            if (RR.list == NULL)
                return top; // SAFE: single read
            extracted = RR.list;
            if (ATOMIC_CAS(ROOT, RR, (extracted->next, true, RR.ts + 1)))
                break;
            continue;
        }
        catch (RootCavity) {
            fill_cavity_cxsy(Root);
            continue;
        }
        int retval = extracted->data;
        delete(extracted);
        return retval;
    }
}
