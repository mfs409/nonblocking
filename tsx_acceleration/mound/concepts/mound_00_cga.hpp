/**
 *  Mound CGA: A mound algorithm described in terms of coarse-grained
 *             atomicity.
 *
 *  [mfs] My goal for this file is to specify everything at a high level, but
 *        completely, using language-level atomicity.
 *
 *  [mfs] There are some very careful points in this code.  In particular,
 *        the code always looks at Node.cavity before any other part of the
 *        Node, and never caches a value across a fill_cavity_cga call.
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
    int   ts     = 0;               // timestamp... unused in CGA
};

/**
 *  Insertion into the mound.
 */
void insert_cga(int n)
{
    atomic {
        // loop until we succeed
        while (true) {
            // for a given mound height, we will try a few times
            for (attempt in 1 ... SOME_LIMIT_BASED_ON_MOUND_HEIGHT) {
                // pick a random leaf
                L = select_random_leaf();
                // if cavity, fix it
                if (L.iscavity)
                    fill_cavity_cga(L);
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
                        // if this node's value equals n, insert here
                        if ((C.list != NULL) && (C.list->data == n)) {
                            N = new List(n, C.list);
                            C.list = N;
                            return;
                        }
                        // look at parent, but be sure it is not cavity
                        P = parentof(C);
                        if (P.iscavity)
                            fill_cavity_cga(P);
                        // if parent < n, insert at child
                        if ((P.list != NULL) && (P.list->data < n)) {
                            N = new List(n, C.list);
                            C.list = N;
                            return;
                        }
                        // if P is root, we can't traverse up, so insert here
                        if (P.is_root) {
                            N = new List(n, P.list);
                            P.list = N;
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
        expand_mound_one_level();
    }
}

/**
 *  Must be called from within an atomic block
 *
 *  Fills a cavity, possibly with recursion
 */
void fill_cavity_cga(Node* N)
{
    // leaves are easy
    if (N.is_leaf) {
        N.is_cavity = false;
        return;
    }
    // assume two children
    // patch up child cavities before looking at their values
    if (N.right->iscavity)
        fill_cavity_cga(N.right);
    if (N.left->iscavity)
        fill_cavity_cga(N.left);
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
        return;
    }
    // node has best value, so just clear the bit and we're good
    N.iscavity = false;
}

/**
 *  Coarse-grained atomic extract-min
 */
int extract_min_cga()
{
    atomic {
        // Root can't be cavity when we extract min
        if (Root.iscavity)
            fill_cavity_cga(root);
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
