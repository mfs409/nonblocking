/**
 *  Mound Refinement 3: Double-checked reads
 *
 *  [mfs] This is a much more aggressive technique for splitting atomic
 *        regions.  We are going to break an atomic section up into several
 *        parts, and chain them together.  To make them work, one atomic
 *        section will return a set of addresses and expected values.  The
 *        second transaction must begin by reading those addresses and
 *        verifying the values.  If the values change, then the first and
 *        second transactions must be redone.
 */

/**
 * Simple linked-list holding integers... our payload is an integer, but
 * could be any word-sized datum
 */
struct List
{
    int   data;
    List* next;
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

int check_if_cavity(Node* N)
{
    if (N->is_cavity)
        return IS_CAVITY;
    return NOT_CAVITY;
}

int compare_value(Node* N, int val)
{
    if (L->list == NULL)
        return VAL_GE;
    if (L->list->data < n)
        return VAL_LT;
    return VAL_GE;
}

int validate_node(Node* N, int nts)
{
    // validate N
    if (N.ts != nts)
        return NODE_CHANGED;
    return NODE_OK;
}

int insert_if_equal(Node* N, int v)
{
    if (N->list == NULL)
        return NOT_EQUAL_N;
    if (N->list->data != v)
        return NOT_EQUAL_N;
    Q = new List(v, N->list);
    N->list = Q;
    N->ts++;
    return INSERT_OK;
}

int check_parent_lt(Node* P, Node* C, int v)
{
    // not sure this is possible...
    if (P->list == NULL)
        return NO_INSERT;
    // TODO: consider fastpath if == n?!?
    if (P->list->data >= n)
        return NO_INSERT;
    Q = new List(v, C->list);
    C->list = Q;
    C.ts++;
    return INSERT_OK;
}

// assumes we already checked that P is >= v
int check_parent_root(Node* P, int v)
{
    if (!P->is_root)
        return NO_INSERT;
    Q = new List(v, P->list);
    P->list = Q;
    P.ts++;
    return INSERT_OK;
}

/**
 *  Insertion into the mound.
 */
void insert_dc(int n)
{
    // pick an initial Leaf.  Note that there is some atomicity needed in the
    // function, though we don't provide the function (yet).
    Node* L = select_random_leaf();

    while (true) {
        int status;
        int lts;
        atomic {
            // make sure Leaf not cavity
            status = check_if_cavity(L);
            if (status == NOT_CAVITY) {
                // make sure Leaf data >= n
                status = compare_value(L, n);
                if (status == VAL_GE) {
                    lts = L.ts;
                    status = LEAF_OK;
                }
            }
        }
        if (status == IS_CAVITY) {
            fill_cavity_dc(L);
            continue;
        }
        if (status == VAL_LT) {
            L = select_random_leaf();
            continue;
        }
        // if we made it here, then we have a leaf with value that is >= n
        // begin traversing up the ancestor chain, until we either reach the
        // root, or find a parent/child pair where the parent value is < n
        // and the child value is >=N
        Node* C = L;
        Node* P = NULL;
        int cts = lts;
        int pts = 0;
        while (true) {
            atomic {
                // make sure the node passed from previous transaction is
                // still valid
                status = validate_node(C, cts);
                if (status == NODE_CHANGED) status = CHILD_CHANGED;
                if (status == NODE_OK) {
                    // if n == C's value, we can insert locally and be done
                    status = insert_if_equal(C, n);
                    if (status == NOT_EQUAL_N) {
                        // need to see what's up with parent
                        P = C->parent;
                        status = check_if_cavity(P);
                        if (status == NOT_CAVITY) {
                            pts = P->ts;
                            status = check_parent_lt(P, C, v);
                            if (status == NO_INSERT) {
                                status = check_parent_root(P, v);
                                if (status == NO_INSERT) {
                                    // need to move up one level
                                    P = C;
                                    pts = cts;
                                    status = TRAVERSE_UP;
                                }
                            }
                        }
                    }
                }
            }
            if (status == CHILD_CHANGED) {
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
            if (status == INSERT_OK)
                return;
            if (status == IS_CAVITY) {
                // P is a cavity... what should we do?
                fill_cavity_dc(P);
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

// step one of cavity fill: handle leaves; note that checking leafness must
// be atomic wrt expand_mound_one_level()
int check_if_leaf(Node* N)
{
    if (N.is_leaf) {
        if (N.is_cavity) {
            N.is_cavity = false;
            N.ts++;
        }
        return IS_LEAF;
    }
    return NOT_LEAF;
}

int check_if_cavity(Node* N)
{
    if (N.is_cavity)
        return IS_CAVITY;
    return NOT_CAVITY;
}

int push_cavity_down(Node* N, int nts, Node* C, int cts)
{
    // validate N
    if (N.ts != nts)
        return CHAIN_FAILED_PARENT;
    // validate Child
    if (C.ts != cts)
        return CHAIN_FAILED_CHILD;
    int x = C->list;
    C->list = x->next;
    x->next = N->list;
    N->list = x;
    N->is_cavity = false;
    C->is_cavity = true;
    N.ts++;
    C.ts++;
    return PUSH_SUCCEEDED;
}

int resolve_cavity_local(Node* N, int nts)
{
    // validate N
    if (N.ts != nts)
        return CHAIN_FAILED_PARENT;
    N->is_cavity = false;
    N.ts++;
    return PUSH_SUCCEEDED;
}


/**
 *  Now this is its own transaction
 *
 *  Fills a cavity, possibly with recursion
 *
 *  NB: there is no atomicity between caller and this, so we must be sure
 *      that N still is a cavity
 */
void fill_cavity_dc(Node* N)
{
    int status = STARTING;
    // note that we do not support reducing the size of a mound, so if it
    // isn't a leaf, we don't need atomicity between the check and subsequent
    // ops.  Furthermore, the check can be outside the /while/, because this
    // isn't going to become a leaf.  However, this could stop being a leaf,
    // so the check needs to be atomic wrt expanding the mound.
    atomic {
        status = check_if_leaf(N);
    }
    if (status == IS_LEAF)
        return;
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
            status = check_if_cavity(N);
            // first make sure N is a cavity
            if (status == IS_CAVITY) {
                nts = READ(N->timestamp);
                nv = (N.list == NULL) ? top : N->list->data;
                // now make sure right isn't a cavity
                status = check_if_cavity(N->right);
                if (status == IS_CAVITY) status = RIGHT_CAVITY;
                if (status == NOT_CAVITY) {
                    rts = READ(N->right->timestamp);
                    rv = (N->right->list == NULL) ? top
                        : N->right->list->data;
                    // now make sure left isn't a cavity
                    status = check_if_cavity(N->left);
                    if (status == IS_CAVITY) status = LEFT_CAVITY;
                    if (status == NOT_CAVITY) {
                        lts = READ(N->left->timestamp);
                        lv = (N->left->list == NULL) ? top
                            : N->left->list->data;
                    }

                }
            }
        }
        if (status == NOT_CAVITY)
            return;
        if (status == RIGHT_CAVITY) {
            fill_cavity_dc(N->right);
            continue;
        }
        if (status == LEFT_CAVITY) {
            fill_cavity_dc(N->left);
            continue;
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
                status = push_cavity_down(N, nts, N->right, rts);
            }
        }
        // pull from left?
        else if ((lv <= rv) && (lv < nv)) {
            atomic {
                status = push_cavity_down(N, nts, N->left, lts);
            }
        }
        // pull from local list?
        else {
            atomic {
                status = resolve_cavity_local(N, nts);
            }
        }
        // if the pull worked, we're good, otherwise we had an inconsistency
        // or conflict, and must start from the top
        if (status == PUSH_SUCCEEDED)
                return;
    }
}

///////////////////////////////////////////////////////
//
// EXTRACT_MIN CODE
//
///////////////////////////////////////////////////////



// step 1 of extract_min_dc is to handle the cavity
//
// call from an atomic block
int handle_root_cavity_dc()
{
    READ(Root.ts);
    if (Root.iscavity)
        return ROOT_HAS_CAVITY;
    return ROOT_NO_CAVITY;
}

// step 2 is to check if the root has a null list, in which case the mound is
// empty and we will return top
int detect_null_root()
{
    if (Root.list == NULL)
        return ROOT_IS_NULL;
    return ROOT_NOT_NULL;
}

// step 3 is to extract the list head from a non-empty root, and return its
// value
List* extract_from_root()
{
    // take the first element off the list, make the root cavity, extract
    // value from first element, return value, and de-allocate the former
    // head node of list
    List* res = Root.list;
    Root.list = res.next;
    Root.iscavity = true;
    return res;
}

/**
 *  atomic extract-min with non-composed call to fill_cavity
 */
int extract_min_dc()
{
    List* extracted = NULL;
    while (true) {
        int status = STARTING;
        atomic {
            status = handle_root_cavity();
            if (status == ROOT_NO_CAVITY) {
                status = detect_null_root();
                if (status == ROOT_NOT_NULL) {
                    extracted = extract_from_root();
                    status = DONE;
                }
            }
        }
        if (status == ROOT_HAS_CAVITY) {
            fill_cavity_dc(Root);
            continue;
        }
        if (status == ROOT_IS_NULL)
            return top;
        if (status == DONE) {
            int retval = extracted->data;
            delete(extracted);
            return retval;
        }
    }
}
