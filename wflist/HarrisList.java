import java.util.concurrent.atomic.*;

/**
 * The Harris-Michael Lock-free Linked List.
 */
class HarrisList implements ISet
{
    /**
     * Internal Node class.
     */
    private static class Node
    {
        private int key;
        private AtomicMarkableReference<Node> next;

        private Node(int k)
        {
            this.key = k;
            this.next = new AtomicMarkableReference<Node>(null, false);
        }
    }

    /** Sentinel nodes. */
    private Node head;
    private Node tail;

    /**
     * Constructor.
     */
    public HarrisList()
    {
        head = new Node(Integer.MIN_VALUE);
        tail = new Node(Integer.MAX_VALUE);
        head.next.set(tail, false);
    }

    /**
     * Insert specified key into the linked list set.
     */
    public boolean insert(int key, int tid)
    {
        Node pred = null, curr = null, succ = null;
        boolean [] marked = {false};
      retry:
        // purpose of outermost while loop is for implementing goto only..
        while (true){
            // initialization
            pred = head;
            curr = pred.next.getReference();
            // traverse linked list
            while (true) {
                succ = curr.next.get(marked);
                while (marked[0]) {
                    if (!pred.next.compareAndSet(curr, succ, false, false))
                        continue retry;
                    curr = succ;
                    succ = curr.next.get(marked);
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
                    node.next = new AtomicMarkableReference<Node>(curr, false);
                    if (pred.next.compareAndSet(curr, node, false, false))
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
        boolean [] marked = {false};
      retry:
        // purpose of outermost while loop is for implementing goto only..
        while (true){
            // initialization
            pred = head;
            curr = pred.next.getReference();
            // traverse linked list
            while (true) {
                succ = curr.next.get(marked);
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
                    node.next = new AtomicMarkableReference<Node>(curr, false);
                    pred.next.compareAndSet(curr, node, false, false);
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
            curr = pred.next.getReference();
            // traverse linked list
            while (true) {
                succ = curr.next.get(marked);
                while (marked[0]) {
                    if (!pred.next.compareAndSet(curr, succ, false, false))
                        continue retry;
                    curr = succ;
                    succ = curr.next.get(marked);
                }
                // continue searching
                if (curr.key < key) {
                    pred = curr;
                    curr = succ;
                }
                // key found: do remove
                else if (curr.key == key) {
                    if (!curr.next.compareAndSet(succ, succ, false, true))
                        continue retry;
                    pred.next.compareAndSet(curr, succ, false, false);
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
        boolean [] marked = {false};
        Node curr = head;
        while (curr.key < key) {
            // move to next one
            curr = curr.next.getReference();
            // check the mark
            curr.next.get(marked);
        }
        return (curr.key == key && !marked[0]);
    }

    public void dump()
    {
        Node curr = head.next.getReference();
        while (curr != null && curr.next.getReference() != null)
        {
            System.out.print(curr.key);
            System.out.print(",");
            System.out.print(curr.next.isMarked());
            System.out.print("->");
            curr = curr.next.getReference();
        }
        System.out.println();
    }
}
