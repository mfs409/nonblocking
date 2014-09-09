import java.util.concurrent.atomic.*;

/**
 * The lock-free linked list algorithm.
 */
class LFList2 implements ISet
{
    /**
     * Internal Node class.
     */
    private static class Node
    {
        private int key;
        private volatile int state;
        private volatile Node next;
        private volatile Node prev;

        private static final AtomicIntegerFieldUpdater<Node> stateUpdater
            = AtomicIntegerFieldUpdater.newUpdater(Node.class, "state");
        private static final AtomicReferenceFieldUpdater<Node, Node> nextUpdater
            = AtomicReferenceFieldUpdater.newUpdater(Node.class, Node.class, "next");
        private static final AtomicReferenceFieldUpdater<Node, Node> prevUpdater
            = AtomicReferenceFieldUpdater.newUpdater(Node.class, Node.class, "prev");

        private boolean casState(int o, int n)
        {
            return stateUpdater.compareAndSet(this, o, n);
        }

        private boolean casNext(Node o, Node n)
        {
            return nextUpdater.compareAndSet(this, o, n);
        }

        private boolean casPrev(Node o, Node n)
        {
            return prevUpdater.compareAndSet(this, o, n);
        }

        private Node(int k, int s)
        {
            this.key = k;
            this.state = s;
        }
    }

    /**
     * States of Nodes.
     */
    private static final int DATA    = 0;
    private static final int DEAD    = 1;
    private static final int INSERT  = 2;
    private static final int REMOVE  = 3;

    private final Node DUMMY = new Node(-1, REMOVE);

    /**
     * List head.
     */
    private volatile Node head;

    private static AtomicReferenceFieldUpdater<LFList2, Node> headUpdater
        = AtomicReferenceFieldUpdater.newUpdater(LFList2.class, Node.class, "head");

    private boolean casHead(Node o, Node n)
    {
        return headUpdater.compareAndSet(this, o, n);
    }

    /**
     * Constructor.
     */
    public LFList2()
    {
        head = new Node(-1, REMOVE);
    }

    /**
     * Insert specified key into the linked list set.
     */
    public boolean insert(int key, int tid)
    {
        Node n = new Node(key, INSERT);
        enlist(n);
        boolean b = helpInsert(n, key);
        int s = b ? DATA : DEAD;
        if (!n.casState(INSERT, s)) {
            helpRemove(n, key);
            n.state = DEAD;
        }
        return b;
    }

    public boolean insert2(int key)
    {
        Node n = new Node(key, INSERT);
        enlist(n);
        boolean b = helpInsert(n, key);
        int s = b ? DATA : DEAD;
        n.state = (b ? DATA : DEAD);
        return b;
    }

    /**
     * Remove specified key from the linked list set.
     */
    public boolean remove(int key, int tid)
    {
        Node n = new Node(key, REMOVE);
        enlist(n);
        boolean b = helpRemove(n, key);
        n.state = DEAD;
        return b;
    }

    /**
     * Search for specified key in the linked list set.
     */
    public boolean contains(int key)
    {
        Node curr = head;
        while (curr != null) {
            if (curr.key == key) {
                int s = curr.state;
                if (s != DEAD)
                    return (s != REMOVE);
            }
            curr = curr.next;
        }
        return false;
    }

    /**
     * Set node at head of the list.
     */
    private void enlist(Node node)
    {
        while (true)
        {
            Node first = head;
            Node prev = first.prev;
            if (first == head) {
                if (prev == null) {
                    if (first.casPrev(null, node)) {
                        node.next = first;
                        casHead(first, node);
                        first.prev = DUMMY;
                        return;
                    }
                }
                else {
                    prev.next = first;
                    casHead(first, prev);
                    first.prev = DUMMY;
                }
            }
        }
    }

    /**
     * The core insert protocol.
     */
    private boolean helpInsert(Node home, int key)
    {
        Node pred = home;
        Node curr = pred.next;
        while (curr != null) {
            int s = curr.state;
            if (s == DEAD) {
                Node succ = curr.next;
                pred.next = succ;
                curr = succ;
            }
            else if (curr.key != key) {
                pred = curr;
                curr = curr.next;
            }
            else {
                return (s == REMOVE);
            }
        }
        return true;
    }

    /**
     * The core remove protocol.
     */
    private boolean helpRemove(Node home, int key)
    {
        Node pred = home;
        Node curr = pred.next;
        while (curr != null) {
            int s = curr.state;
            if (s == DEAD) {
                Node succ = curr.next;
                pred.next = succ;
                curr = succ;
            }
            else if (curr.key != key) {
                pred = curr;
                curr = curr.next;
            }
            else if (s == DATA) {
                curr.state = DEAD;
                return true;
            }
            else if (s == REMOVE) {
                return false;
            }
            else /* if (s == INSERT) */ {
                if (curr.casState(INSERT, REMOVE))
                    return true;
            }
        }
        return false;
    }

    public void dump()
    {
        Node curr = head;
        while (curr != null)
        {
            System.out.print(curr.key);
            System.out.print(",");
            System.out.print(curr.state);
            System.out.print("->");
            curr = curr.next;
        }
        System.out.println();
    }

}
