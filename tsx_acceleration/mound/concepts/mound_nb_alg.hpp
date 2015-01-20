/**
 * Simple linked-list holding integers... our payload is an integer, but
 * could be any word-sized datum
 */
struct List
{
    int   data;
    Node* next;
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
 *  Insert to the mound.
 *
 *  [mfs] Here's my guess at how this is supposed to work.
 *    A - Pick a random leaf
 *      - Next get that leaf's parent
 *      - let (l, p) be a pair representing that leaf and its parent
 *      - if l is a cavity, that's OK.
 *    B - if p is a cavity, fill it and restart
 *      - if p's value is < n, then we will insert at l with (l,p) unchanged
 *      - if p's value equals n, then we will insert at p with p unchanged
 *      - if p's value > n, then if p is root we will insert at p with p
 *        unchanged
 *      - if p's value > n, and p not root, then let l=p; let p = parentof(p);
 *        go to point B
 *      - Note that the above is not entirely correct, because l or p might
 *        become a cavity and we haven't really been rigorous with atomicity.
 *    C - To insert, we just need to push our value to the head of the list at
 *        the insertion point, while keeping all else unchanged
 *      - Note that we didn't mention expanding the mound, which might be
 *        needed
 *
 *  [mfs] That's very complicated.  It might be good to start at a higher
 *        level.
 */
void add(int n)
{
  retry:
    // find the child-parent pair where n can be pushed on
    // head of child list
    Node * child = find_insertion_node(n);

    // Attempt to execute true insertion, retry if failed
    if (!do_insert(child, n))
        goto retry;
}

/**
 *  Find the node to insert (with parent < n).
 */
Node* find_insertion_node(int n)
{
  retry:
    // pick a random leaf >= n
    Node* child  = random_leaf(n);
    Node* parent = parentof(child);

    // search along the path towards root
    while (!is_root(parent)) {
        Node p = fill_cavity(parent);
        if (n > p.list->data)
            return child;
        child = parent;
        parent = parentof(parent);
    }
    goto retry;
}

/**
 * Execute the insertion, return true if succeeded,
 * otherwise return false.
 */
bool do_insert(Node* child, int n)
{
    Node* parent;
    Node  c, p;
  retry:
    // if parent or child is cavity, fill it first
    parent = parentof(child);
    c = READ(child);
    p = READ(parent);
    if (p.cavity) {
        p = fill_cavity(parent);
        c = fill_cavity(child);
    }
    else if (c.cavity) {
        c = fill_cavity(child);
    }

    // the child-parent pair is no longer valid for insert
    if (n > c.list->data || n <= p.list->data)
        return false;

    // use a DCAS to:
    //  1) push newnode to child
    //  2) increment the version number of parent
    List* newnode = new List(n, c.list);
    if (DCAS(child,  c, (newnode, false, c.ts+1),
             parent, p, (p.list,  false, p.list+1)))
        return true;
    // dcas failed, retry
    goto retry;
}

/**
 *  Extract min from mound.
 *
 *  [mfs] Here's my guess at how this is supposed to work
 *      - If the root is a cavity, fill the cavity and restart
 *      - At the root, replace the current value with a cavity
 *      - The value that we extracted is the eventual return value
 *      - We could just return here
 *      - Otherwise, we need to fill the cavity
 */
int remove()
{
    // mark root as cavity
    while (true) {
        // if root is cavity, fill it first
        r = fill_cavity(root);

        // mark the root node as a cavity (linearize here)
        if (CAS(root, r, (r.list, true, r.ts+1))) {
            result = r.list->data;
            break;
        }
    }

    // Call fill_cavity function on root
    fill_cavity(root);
    return result;
}

/**
 * Restore the invariant for a node that marked as cavity.  If the node is
 * already not a cavity, the function simply returns the node without taking
 * side effects (identical to a READ).
 *
 *  [mfs] Here's my guess at how this is supposed to work
 *      - If the current node is not cavity, return
 *      - If the current node is a leaf, and it has values in its list, simply
 *        mark it 'not cavity'
 *      - If the current node is a leaf, and it has a null list, also mark
 *        'not cavity'
 *      - Otherwise, this is not a leaf.  If either child is a cavity, fill
 *        the child cavity first.
 *      - Assuming that this and its children are both not cavity, then we can
 *        progress
 *      - Figure out which of this, left, right has the smallest value
 *      - If 'this', then make head of list the value of this node, and clear
 *        cavity bit.  We're good.
 *      - [mfs] I'm starting to worry about downward recursion when there is
 *              very little in the mound.  Linearization is going to be
 *              tricky.
 *      - Otherwise, one of the children has a smaller value than the parent.
 *      - To remedy the problem, we need to atomically move the child's list
 *        head to the parent's list head.  Whatever child we modified needs to
 *        be marked cavity.
 */
Node fill_cavity(Node* parent)
{
    Node p, c;
    // return if node is not cavity
    p = READ(parent);
    if (!p.cavity)
        return p;
    // if we are at leaf, clear the cavity bit and return
    if (is_leaf(parent)) {
        CAS(parent, p, (p.list, false, p.ts+1));
        return (p.list, false, p.ts+1);
    }

    // compute the minimum among children
    Node* which = parent;
    // which is the minimum
    int   min   = p.list->data;
    // min of children
    for (each Node* child in childrenof(node)) {
        // if child is cavity, fill it first
        c = fill_cavity(child);

        // peek head of child
        if (c.list->data < min) {
            which = child;
            min   = c.list->data;
        }
    }

    // pull from local list
    if (which == parent) {
        if (CAS(parent, p, (p.list->next, false, p.ts+1)))
            return p;
    }
    // pull from child, using DCAS
    else {
        if (DCAS(which,  c, (c.list->next, true,  c.ts+1),
                 parent, p, (c.list,       false, p.ts+1)))
            return p;
        fill_cavity(which);
    }
}
