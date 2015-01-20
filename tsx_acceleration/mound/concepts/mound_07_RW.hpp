/**
 *  Mound Refinement 7: Modify each atomic region so that reads and writes of
 *                      nodes are performed via explicit READ/WRITE ops
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
void insert_RW(int n)
{
    // pick an initial Leaf.  Note that there is some atomicity needed in the
    // function, though we don't provide the function (yet).
    Node* L = select_random_leaf();
    while (true) {
        nodecache_t LL;
        atomic {
            LL = L->READ();
            // make sure Leaf not cavity
            if (LL->is_cavity) throw LeafIsCavity;
            // make sure Leaf data >= n
            //
            // NB: there is a possible erroneous throw here.  Suppose I read
            // LL, and then L changes so that L->list is unused.  Then let
            // L->list be recycled, so that it sits somewhere else in the
            // world.  The atomic will abort, but if I stop using atomics,
            // I'll need to double-check that LL == L before throwing
            // LeafTooBig.  Otherwise, a simple continue will suffice.
            if ((LL->list != NULL) && (LL->list->data < n)) throw LeafTooBig;
        }
        catch (LeafIsCavity) {
            fill_cavity_RW(L); continue;
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
            atomic {
                // make sure the node passed from previous transaction is
                // still valid
                nodecache_t TT = C->READ();
                if (CC.ts != TT.ts) throw ChildChanged;
                // if n == C's value, we can insert locally and be done
                if ((CC->list != NULL) && (CC->list->data == n)) {
                    C->WRITE(new List(n, CC->list), false, CC.ts+1);
                    return;
                }

                // need to see what's up with parent
                P = parentof(C);
                PP = P->READ();
                // NB: if C changed, then maybe P isn't a cavity anymore...
                //     Better atomicity could help here.
                if (PP.is_cavity) throw ParentCavity;

                // can we insert at C?
                //
                // NB: again, possible atomicity bug here if PP changes?  If
                // we drop 'atomic' blocks, do we need to validate PP after
                // reading PP->list->data before doing the write?
                if ((PP.list != NULL) && (PP.list->data < n)) {
                    C->WRITE(new List(n, CC->list), false, CC.ts+1);
                    return;
                }

                // is P root?  If so, insert at P
                if (is_root(P)) {
                    P->WRITE(new List(n, PP.list), false, PP.ts+1);
                    return;
                }
                // need to move up one level
                P = C;
                PP = CC;
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
                fill_cavity_RW(P);
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
void fill_cavity_RW(Node* N)
{
    // note that we do not support reducing the size of a mound, so if it
    // isn't a leaf, we don't need atomicity between the check and subsequent
    // ops.  Furthermore, the check can be outside the /while/, because this
    // isn't going to become a leaf.  However, this could stop being a leaf,
    // so the check needs to be atomic wrt expanding the mound.
    atomic {
        if (is_leaf(N)) {
            nodecache_t NN = N->READ();
            if (NN.is_cavity) {
                N->WRITE(NN.list, false, NN.ts + 1);
            }
            return;
        }
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
        atomic {
            // N must be a cavity
            NN = N->READ();
            if (!NN.is_cavity) return;
            nv = (NN.list == NULL) ? top : NN.list->data;
            // right can't be a cavity
            RR = R->READ();
            if (RR.is_cavity)
                throw RightCavity;
            rv = (RR.list == NULL) ? top : RR.list->data;
            // left can't be a cavity
            LL = L->READ();
            if (LL.is_cavity)
                throw LeftCavity;
            lv = (LL.list == NULL) ? top : LL.list->data;
        }
        catch (RightCavity) {
            fill_cavity_RW(N->right); continue;
        }
        catch (LeftCavity) {
            fill_cavity_RW(N->left); continue;
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
            atomic {
                // make sure N and R unchanged
                nodecache_t TT = N->READ();
                if (NN.ts != TT.ts) continue;
                TT = R->READ();
                if (RR.ts != TT.ts) continue;

                // move R's list head into N.  Note that we can't swap, but
                // instead copy the node
                List* x = RR.list;
                List* new_x = new List(x->data, NN.list);
                N->WRITE(new_x, false, NN.ts + 1);
                R->WRITE(x->next, true, RR.ts + 1);
                free(x);
                return;
            }
        }
        // pull from left?
        if ((lv <= rv) && (lv < nv)) {
            atomic {
                // make sure N and L unchanged
                nodecache_t TT = N->READ();
                if (NN.ts != TT.ts) continue;
                TT = L->READ();
                if (LL.ts != TT.ts) continue;

                // move L's list head into N.  Note that we can't swap, but
                // instead copy the node
                List* x = LL.list;
                List* new_x = new List(x->data, NN.list);
                N->WRITE(new_x, false, NN.ts + 1);
                L->WRITE(x->next, true, LL.ts + 1);
                free(x);
                return;
            }
        }
        // pull from local list?
        atomic {
            // make sure N unchanged
            nodecache_t TT = N->READ();
            if (NN.ts != TT.ts) continue;

            // just clear the cavity and we're good
            N->WRITE(NN.list, false, NN.ts + 1);
            return;
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
int extract_min_RW()
{
    List* extracted = NULL;
    while (true) {
        atomic {
            nodecache_t RR = ROOT->READ();
            if (RR.iscavity)
                throw RootCavity;
            if (RR.list == NULL)
                return top;
            extracted = RR.list;
            ROOT->WRITE(extracted->next, true, RR.ts + 1);
        }
        catch (RootCavity) {
            fill_cavity_RW(Root);
            continue;
        }
        int retval = extracted->data;
        delete(extracted);
        return retval;
    }
}
