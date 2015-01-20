/**
 *  Mound Refinement 2: Split the insert method
 *
 *  [mfs] Now we are going to start splitting atomic sections up.  At this
 *        stage, the splitting is pretty simple.  We split the insert()
 *        method, and that's it.  Now each iteration of the for() loop is a
 *        transaction, and expanding the mound is its own transaction.
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

/**
 *  Insertion into the mound.
 *
 *  [mfs] We perform a split here.  The individual attempts are each a
 *        separate atomic block, as is mound expansion
 */
void insert_si(int n)
{
    // loop until we succeed
    while (true) {
        // for a given mound height, we will try a few times
        for (attempt in 1 ... SOME_LIMIT_BASED_ON_MOUND_HEIGHT) {
            atomic {
                // pick a random leaf
                L = select_random_leaf();
                // if cavity, fix it
                READ(L.ts);
                if (L.iscavity)
                    fill_cavity_si(L);
                // this attempt can only succeed if L.list->data >= n
                //
                // [mfs] Note that since extract-min can leave intermediate
                //       gaps in the ancestor chain, we need to check for
                //       NULL lists
                if ((L.list == NULL) || (L.list->data >= n)) {
                    // work on children and parents
                    C = L;
                    // traverse up ancestor chain until we find a good
                    // insertion point
                    while (true) {
                        READ(C.ts);
                        // if this node's value equals n, insert here
                        if ((C.list != NULL) && (C.list->data == n)) {
                            N = new List(n, C.list);
                            C.list = N;
                            C.ts++;
                            return;
                        }
                        // look at parent, but be sure it is not cavity
                        P = parentof(C);
                        READ(P.ts);
                        if (P.iscavity)
                            fill_cavity_si(P);
                        // if parent < n, insert at child
                        if ((P.list != NULL) && (P.list->data < n)) {
                            N = new List(n, C.list);
                            C.list = N;
                            C.ts++;
                            return;
                        }
                        // if P is root, we can't traverse up, so insert here
                        if (P.is_root) {
                            N = new List(n, P.list);
                            P.list = N;
                            P.ts++;
                            return;
                        }
                        // move up one level and keep working
                        C = P;
                    }
                }
            }
        }
        // all our random probing failed, so make the mound bigger and start
        // over
        atomic {
            expand_mound_one_level();
        }
    }
}

/**
 *  Must be called from within an atomic block
 *
 *  Fills a cavity, possibly with recursion
 */
void fill_cavity_si(Node* N)
{
    READ(N.ts);
    // leaves are easy
    if (N.is_leaf) {
        N.is_cavity = false;
        N.ts++;
        return;
    }
    // assume two children
    // patch up child cavities before looking at their values
    READ(N.right->ts)
    if (N.right->iscavity)
        fill_cavity_si(N.right);
    READ(N.left->ts);
    if (N.left->iscavity)
        fill_cavity_si(N.left);
    // look at child values and nv's value
    nv = (N.list == NULL) ? top : N.list->data;
    rv = (N.right->list == NULL) ? top : N.right->list->data;
    lv = (N.left->list == NULL) ? top : N.left->list->data;
    // pull from right?
    if ((rv <= lv) && (rv < nv)) {
        x = N.right->list;
        N.right->list = x->next;
        x->next = N.list;
        N.list = x;
        N.iscavity = false;
        N.right->iscavity = true;
        N.ts++;
        N.right->ts++;
        return;
    }
    // pull from left?
    if ((lv <= rv) && (lv < nv)) {
        x = N.left->list;
        N.left->list = x->next;
        x->next = N.list;
        N.list = x;
        N.iscavity = false;
        N.left->iscavity = true;
        N.ts++;
        N.left->ts++;
        return;
    }
    // node has best value, so just clear the bit and we're good
    N.iscavity = false;
    N.ts++;
}

/**
 *  Coarse-grained atomic extract-min
 */
int extract_min_si()
{
    atomic {
        READ(Root.ts);
        // Root can't be cavity when we extract min
        if (Root.iscavity)
            fill_cavity_si(root);
        // empty root list means empty mound
        if (Root.list == NULL)
            return top;
        // take the first element off the list, make the root cavity, extract
        // value from first element, return value, and de-allocate the former
        // head node of list
        List* res = Root.list;
        Root.list = res.next;
        Root.iscavity = true;
        int ans = res->data;
        delete(res);
        return ans;
    }
}
