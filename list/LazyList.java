import java.util.concurrent.atomic.*;

class LazyList implements ISet
{
    private static class Node
    {
        private int key;
        private Node next;
        private boolean marked;
        private volatile int lock;

        private static final AtomicIntegerFieldUpdater<Node> lockUpdater
            = AtomicIntegerFieldUpdater.newUpdater(Node.class, "lock");

        private Node(int k)
        {
            this.key = k;
        }

        private void lock()
        {
            // simple spin lock
            while (!lockUpdater.compareAndSet(this, 0, 1)) {
                while (this.lock == 1);
            }
        }

        private void unlock()
        {
            this.lock = 0;
        }
    }

    /** Sentinel nodes.*/
    private Node head;
    private Node tail;

    /**
     * Constructor.
     */
    public LazyList()
    {
        head = new Node(Integer.MIN_VALUE);
        tail = new Node(Integer.MAX_VALUE);
        head.next = tail;
    }

    private boolean validate(Node pred, Node curr)
    {
        return !pred.marked && !curr.marked && pred.next == curr;
    }

    public boolean insert(int key, int tid)
    {
        while (true) {
            Node pred = head;
            Node curr = head.next;
            while (curr.key < key) {
                pred = curr;
                curr = curr.next;
            }
            pred.lock();
            curr.lock();
            if (validate(pred, curr)) {
                if (curr.key == key) {
                    curr.unlock();
                    pred.unlock();
                    return false;
                }
                else {
                    Node n = new Node(key);
                    n.next = curr;
                    pred.next = n;
                    curr.unlock();
                    pred.unlock();
                    return true;
                }
            }
            curr.unlock();
            pred.unlock();
        }
    }

    public boolean insert2(int key)
    {
        while (true) {
            Node pred = head;
            Node curr = head.next;
            while (curr.key < key) {
                pred = curr;
                curr = curr.next;
            }
            if (curr.key == key) {
                return false;
            }
            else {
                Node n = new Node(key);
                n.next = curr;
                pred.next = n;
                return true;
            }
        }
    }

    public boolean remove(int key, int tid)
    {
        while (true) {
            Node pred = head;
            Node curr = head.next;
            while (curr.key < key) {
                pred = curr;
                curr = curr.next;
            }
            pred.lock();
            curr.lock();
            if (validate(pred, curr)) {
                if (curr.key != key) {
                    curr.unlock();
                    pred.unlock();
                    return false;
                }
                else {
                    curr.marked = true;
                    pred.next = curr.next;
                    curr.unlock();
                    pred.unlock();
                    return true;
                }
            }
            curr.unlock();
            pred.unlock();
        }
    }

    public boolean contains(int key)
    {
        Node curr = head;
        while (curr.key < key)
            curr = curr.next;
        return curr.key == key && !curr.marked;
    }
}
