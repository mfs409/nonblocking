import java.util.concurrent.atomic.*;

/**
 * The Harris-Michael Lock-free linked list with RTTI optimization.
 */
class HarrisListRTTI implements ISet
{
    /**
     * Internal Node class.
     */
    private static class Node
    {
        protected int key;
        protected volatile Node next;

        private static final AtomicReferenceFieldUpdater<Node, Node> nextUpdater
            = AtomicReferenceFieldUpdater.newUpdater(Node.class, Node.class, "next");

        private Node(int k)
        {
            this.key = k;
        }

        private boolean casNext(Node o, Node n)
        {
            return nextUpdater.compareAndSet(this, o, n);
        }
    }

    private static class Marker extends Node
    {
        private Marker(Node n)
        {
            super(Integer.MIN_VALUE);
            this.next = n;
        }
    }

    /** Sentinel nodes. */
    private Node head;
    private Node tail;

    /**
     * Constructor.
     */
    public HarrisListRTTI()
    {
        head = new Node(Integer.MIN_VALUE);
        tail = new Node(Integer.MAX_VALUE);
        head.next = tail;
    }

    /**
     * Insert specified key into the linked list set.
     */
    public boolean insert(int key, int tid)
    {
        Node pred = null, curr = null, succ = null;
      retry:
        // purpose of outermost while loop is for implementing goto only..
        while (true){
            // initialization
            pred = head;
            curr = pred.next;
            // traverse linked list
            while (true) {
                succ = curr.next;
                while (succ instanceof Marker) {
                    succ = succ.next;
                    // snip curr and marker
                    if (!pred.casNext(curr, succ))
                        continue retry;
                    curr = succ;
                    succ = succ.next;
                }
                // continue searching
                if (curr.key < key) {
                    pred = curr;
                    curr = succ;
                }
                // key exists
                else if (curr.key == key)
                    return false;
                // locate a window: do insert
                else {
                    Node node = new Node(key);
                    node.next = curr;
                    if (pred.casNext(curr, node))
                        return true;
                    else
                        continue retry;
                }
            }
        }
    }

    public boolean insert2(int key)
    {
        Node pred = null, curr = null, succ = null;
      retry:
        // purpose of outermost while loop is for implementing goto only..
        while (true){
            // initialization
            pred = head;
            curr = pred.next;
            // traverse linked list
            while (true) {
                succ = curr.next;
                // continue searching
                if (curr.key < key) {
                    pred = curr;
                    curr = succ;
                }
                // key exists
                else if (curr.key == key)
                    return false;
                // locate a window: do insert
                else {
                    Node node = new Node(key);
                    node.next = curr;
                    pred.casNext(curr, node);
                    return true;
                }
            }
        }
    }

    /**
     * Remove specified key from the linked list set.
     */
    public boolean remove(int key, int tid)
    {
        Node pred = null, curr = null, succ = null;
        boolean [] marked = {false};
      retry:
        // purpose of outermost while loop is for implementing goto only..
        while (true){
            // initialization
            pred = head;
            curr = pred.next;
            // traverse linked list
            while (true) {
                succ = curr.next;
                while (succ instanceof Marker) {
                    succ = succ.next;
                    if (!pred.casNext(curr, succ))
                        continue retry;
                    curr = succ;
                    succ = succ.next;
                }
                // continue searching
                if (curr.key < key) {
                    pred = curr;
                    curr = succ;
                }
                // key found: do remove
                else if (curr.key == key) {
                    if (!curr.casNext(succ, new Marker(succ)))
                        continue retry;
                    pred.casNext(curr, succ);
                    return true;
                }
                // key not found
                else if (curr.key > key) {
                    return false;
                }
            }
        }
    }

    /**
     * Search for specified key in the linked list set.
     */
    public boolean contains(int key)
    {
        Node curr = head;
        while (curr.key < key) {
            curr = curr.next;
        }
        return (curr.key == key && (!(curr.next instanceof Marker)));
    }

    public void dump()
    {
        Node curr = head.next;
        while (curr != null && (!(curr instanceof Marker)) && curr.next != null)
        {
            System.out.print(curr.key);
            System.out.print("->");
            curr = curr.next;
        }
        System.out.println();
    }
}
