/**
 *  Mound CGA: A mound algorithm described in terms of coarse-grained
 *             atomicity.
 *
 *  [mfs] My goal for this file is to specify everything at a high level, but
 *        completely, using language-level atomicity.
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
    int   ts     = 0;               // timestamp
};

/**
 *  Perhaps 'simple' is a misnomer.  This is an insert function that uses
 *  coarse-grained atomicity to describe its behavior.
 */
void insert_simple(int n)
{
    // loop until we succeed
    while (true) {
        // for a given mound height, we will try a few times
        int attempt = 1;
        while (attempt < SOME_LIMIT_BASED_ON_MOUND_HEIGHT) {
            atomic {
                // pick a random leaf
                //
                // NB: 'attempt#threadid' is shorthand to indicate that
                //     sometimes we will try the same leaf multiple times,
                //     without changing 'attempt'
                L = select_random_leaf(attempt#threadid);
                // if cavity, fix it and restart
                //
                // [mfs] restart not necessary
                if (L.iscavity) {
                    FillCavity(L); // will commit and try same leaf
                }
                // if the leaf's value < n, then this attempt failed
                else if (L.value < n) {
                    attempt++; // will commit and try another leaf
                }
                else {
                    C = L;
                    // traverse up ancestor chain until we find a good
                    // insertion point
                    while (true) {
                        // if this node's value equals n, insert here
                        if (C.value == n) {
                            N = new List(n, C->list);
                            C->list = N;
                            return;
                        }
                        // look at parent
                        P = parentof(C);
                        // If the parent is a cavity, fill it and restart
                        //
                        // [mfs] restart not necessary
                        if (P.iscavity) {
                            FillCavity(P);
                            break; // will commit and try same leaf
                        }
                        // if parent < n, insert at child
                        if (P.value < n) {
                            N = new List(n, C->list);
                            C->list = N;
                            return;
                        }
                        // if P is root, we can't traverse up, so insert here
                        if (P.is_root) {
                            N = new List(n, P->list);
                            P->list = N;
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
void FillCavity(Node* N)
{
    // no activity needed if node is not a cavity...
    //
    // [mfs] not strictly needed with coarse-grained atomicity
    if (!N.iscavity) return;

    // leaves are easy
    if (N.is_leaf) {
        N.is_cavity = false;
        return;
    }

    // assume two children
    // patch up child cavities before looking at their values
    if (N.right.iscavity)
        FillCavity(N.right);
    if (N.left.iscavity)
        FillCavity(N.left);
    // look at child values and nv's value
    nv = (N.list == NULL) ? top : N.list->data;
    rv = (N.right.list == NULL) ? top : N.right.list->data;
    lv = (N.left.list == NULL) ? top : N.left.list->data;
    // pull from right?
    if ((rv > lv) && (rv > nv)) {
        x = N.right.list;
        N.right.list = x->next;
        x->next = N.list;
        N.list = x;
        N.iscavity = false;
        N.right.iscavity = true;
        return;
    }
    // pull from left?
    if ((lv > rv) && (lv > nv)) {
        x = N.left.list;
        N.left.list = x->next;
        x->next = N.list;
        N.list = x;
        N.iscavity = false;
        N.left.iscavity = true;
        return;
    }
    // node has best value, so just clear the bit and we're good
    N.iscavity = false;
}

/**
 *  Coarse-grained atomic extract-min
 */
int remove_simple()
{
    atomic {
        // Root can't be cavity when we extract min
        if (Root.iscavity)
            FillCavity(root);
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
        free(res);
        return ans;
    }
}

