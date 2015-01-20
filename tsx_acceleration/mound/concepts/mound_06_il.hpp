/**
 *  Mound Refinement 6: Immutable Lists: every List node is immutable
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
    List* list   = null;            // pointer to head node
    bool  cavity = false;           // indicate whether the node is a cavity
    int   ts     = 0;               // timestamp... useful for breaking up
                                    // atomic sections
};

///////////////////////////////////////////////////////
//
// INSERT CODE
//
///////////////////////////////////////////////////////

/**
 *  Insertion into the mound.
 */
void insert_il(int n)
{
    // pick an initial Leaf.  Note that there is some atomicity needed in the
    // function, though we don't provide the function (yet).
    Node* L = select_random_leaf();
    while (true) {
        int lts;
        atomic {
            // make sure Leaf not cavity
            if (L->is_cavity) throw LeafIsCavity;
            // make sure Leaf data >= n
            if ((L->list != NULL) && (L->list->data < n)) throw LeafTooBig;
            lts = L.ts;
        }
        catch (LeafIsCavity) {
            fill_cavity_il(L); continue;
        }
        catch (LeafTooBig) {
            L = select_random_leaf(); continue;
        }
        // if we made it here, then we have a leaf with value that is >= n
        // begin traversing up the ancestor chain, until we either reach the
        // root, or find a parent/child pair where the parent value is < n
        // and the child value is >=N
        Node *C = L, *P = NULL;
        int cts = lts, pts = 0;
        while (true) {
            atomic {
                // make sure the node passed from previous transaction is
                // still valid
                if (C.ts != cts) throw ChildChanged;
                // if n == C's value, we can insert locally and be done
                if ((C->list != NULL) && (C->list->data == n)) {
                    Q = new list(n, C->list);
                    C->list = Q;
                    C->ts++;
                    return;
                }
                // need to see what's up with parent
                P = C->parent;
                if (P->is_cavity) throw ParentCavity;
                pts = P->ts;
                // can we insert at C?
                if ((P->list != NULL) && (P->list->data < n)) {
                    Q = new list(n, C->list);
                    C->list = Q;
                    C->ts++;
                    return;
                }
                // is P root?
                if (P->is_root) {
                    Q = new list(n, P->list);
                    P->list = Q;
                    P->ts++;
                    return;
                }
                // need to move up one level
                P = C;
                pts = cts;
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
                fill_cavity_il(P);
                continue;  // 33% chance that we changed cts.  Often, the
                           // change to cts is benign.  Can we work around?
                           // TODO

                // I think the optimization here is that we say 'if P >= n,
                // make C = P, update cts to pts, and then wrap around.  Else
                // if C >= n, update cts and move on.  Else break!
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
void fill_cavity_il(Node* N)
{
    // note that we do not support reducing the size of a mound, so if it
    // isn't a leaf, we don't need atomicity between the check and subsequent
    // ops.  Furthermore, the check can be outside the /while/, because this
    // isn't going to become a leaf.  However, this could stop being a leaf,
    // so the check needs to be atomic wrt expanding the mound.
    atomic {
        if (N.is_leaf) {
            if (N.is_cavity) {
                N.is_cavity = false;
                N.ts++;
            }
            return;
        }
    }
    // Now comes the hard work.
    while (true) {
        // timestamps
        int nts, rts, lts;
        // values
        int nv, rv, lv;
        // If N isn't a cavity, we're done.  Otherwise, to fill the cavity at
        // N, we must first be sure that N's children are both not cavities,
        // and we need to know the values in the heads of the lists at N and
        // N's children.  This transaction + compensation can only be passed
        // when all of the above are achieved
        atomic {
            // N must be a cavity
            if (!N->is_cavity) return;
            nts = READ(N->timestamp);
            nv = (N.list == NULL) ? top : N->list->data;
            // right can't be a cavity
            if (N->right->is_cavity)
                throw RightCavity;
            rts = READ(N->right->timestamp);
            rv = (N->right->list == NULL) ? top : N->right->list->data;
            // left can't be a cavity
            if (N->left->is_cavity)
                throw LeftCavity;
            lts = READ(N->left->timestamp);
            lv = (N->left->list == NULL) ? top : N->left->list->data;
        }
        catch (RightCavity) {
            fill_cavity_il(N->right); continue;
        }
        catch (LeftCavity) {
            fill_cavity_il(N->left); continue;
        }

        // nts, rts, lts all valid; nv, rv, lv all valid.  We can easily
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
                if (N->ts == nts) {
                    if (N->right->ts == rts) {
                        List* x = N->right->list;
                        List* new_x = new List(x->data, N->list);
                        N->right->list = x->next;
                        N->list = new_x;
                        N->is_cavity = false;
                        N->right->is_cavity = true;
                        N->ts++;
                        N->right->ts++;
                        free(x);
                        return;
                    }
                }
            }
        }
        // pull from left?
        else if ((lv <= rv) && (lv < nv)) {
            atomic {
                if (N->ts == nts) {
                    if (N->left->ts == lts) {
                        List* x = N->left->list;
                        List* new_x = new List(x->data, N->list);
                        N->left->list = x->next;
                        N->list = new_x;
                        N->is_cavity = false;
                        N->left->is_cavity = true;
                        N->ts++;
                        N->left->ts++;
                        free(x);
                        return;
                    }
                }
            }
        }
        // pull from local list?
        else {
            atomic {
                if (N.ts == nts) {
                    N->is_cavity = false;
                    N->ts++;
                    return;
                }
            }
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
int extract_min_il()
{
    List* extracted = NULL;
    while (true) {
        atomic {
            if (Root.iscavity)
                throw RootCavity;
            if (Root.list == NULL)
                return top;
            extracted = Root.list;
            Root.list = extracted.next;
            Root.iscavity = true;
        }
        catch (RootCavity) {
            fill_cavity_il(Root);
            continue;
        }
        int retval = extracted->data;
        delete(extracted);
        return retval;
    }
}
