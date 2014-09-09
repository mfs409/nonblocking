import java.util.concurrent.atomic.*;

/**
 * The wait-free linked list algorithm.
 */
class AdaptiveWFList implements ISet
{
    private static class Node
    {
        private int key;
        private volatile int state;
        private volatile Node next;
        private volatile Node prev;
        private int tid;
        private volatile long phase;

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

        private Node(int k, int s, int t)
        {
            this.key = k;
            this.state = s;
            this.tid = t;
        }
    }

    private class HelpRecord
    {
        int curTid;
        long lastPhase;
        long nextCheck;

        private HelpRecord()
        {
            curTid = -1;
            reset();
        }

        private void reset()
        {
            curTid = (curTid + 1) % ListBench.THREAD_NUM;
            lastPhase = state.get(curTid).phase;
            nextCheck = HELPING_DELAY;
        }
    }


    private static final int MAX_TRIAL = 256;
    private static final int HELPING_DELAY = 128;


    /**
     * States of Nodes.
     */
    private static final int DATA    = 0;
    private static final int DEAD    = 1;
    private static final int INSERT  = 2;
    private static final int REMOVE  = 3;

    private final Node DUMMY = new Node(-1, REMOVE, -1);
    /**
     * List head.
     */
    private volatile Node head;
    private volatile long maxPhase;
    private AtomicReferenceArray<Node> state;
    private HelpRecord [] helpRecords;

    private static AtomicLongFieldUpdater<AdaptiveWFList> maxPhaseUpdater
        = AtomicLongFieldUpdater.newUpdater(AdaptiveWFList.class, "maxPhase");
    private static AtomicReferenceFieldUpdater<AdaptiveWFList, Node> headUpdater
        = AtomicReferenceFieldUpdater.newUpdater(AdaptiveWFList.class, Node.class, "head");

    private boolean casHead(Node o, Node n)
    {
        return headUpdater.compareAndSet(this, o, n);
    }

    /**
     * Constructor.
     */
    public AdaptiveWFList()
    {
        head = new Node(-1, REMOVE, -1);
        maxPhase = 0;
        state = new AtomicReferenceArray<Node>(ListBench.THREAD_NUM);
        helpRecords = new HelpRecord[ListBench.THREAD_NUM];
        for (int i = 0; i < state.length(); i++) {
            Node n = new Node(0, 0, -1);
            n.phase = Long.MAX_VALUE;
            state.set(i, n);
            helpRecords[i] = new HelpRecord();
        }
    }

    private void enlist(Node node, int tid)
    {
        helpIfNeeded(tid);

        int trial = 0;
        while (++trial < MAX_TRIAL)
        {
            Node first = head;
            Node prev = first.prev;
            if (first == head) {
                if (prev == null) {
                    if (first.casPrev(null, node)) {
                        // fast path fixing
                        node.next = first;
                        casHead(first, node);
                        first.prev = DUMMY;
                        return;
                    }
                }
                else {
                    helpFinish(first, prev);
                }
            }
        }

        // switch to slow path
        enlistSlow(node);
    }

    void helpIfNeeded(int tid)
    {
        HelpRecord rec = helpRecords[tid];
        if (rec.nextCheck-- == 0) {
            Node n = state.get(rec.curTid);
            if (n.phase != Long.MAX_VALUE && n.phase == rec.lastPhase)
                helpEnlist(rec.curTid, rec.lastPhase);
            rec.reset();
        }
    }

    private void enlistSlow(Node node)
    {
        long phase = maxPhaseUpdater.getAndIncrement(this);
        node.phase = phase;
        state.set(node.tid, node);
        for (int i = 0; i < state.length(); i++)
            helpEnlist(i, phase);
        helpFinish();
    }

    private void helpEnlist(int tid, long phase)
    {
        Node n =  state.get(tid);
        while (n.phase <= phase) {
            Node first = head;
            Node prev = first.prev;
            if (first == head) {
                if (prev == null) {
                    if (n.phase <= phase) {
                        if (first.casPrev(null, n)) {
                            helpFinish(first, n);
                            return;
                        }
                    }
                }
                else {
                    helpFinish(first, prev);
                }
            }
        }
    }

    private void helpFinish()
    {
        Node first = head;
        Node prev = first.prev;
        helpFinish(first, prev);
    }

    private void helpFinish(Node first, Node prev)
    {
        if (prev != null && prev != DUMMY) {
            int tid = prev.tid;
            if (tid != -1) {
                Node n = state.get(tid);
                if (n == prev) {
                    n.phase = Long.MAX_VALUE;
                    prev.next = first;
                    casHead(first, prev);
                    first.prev = DUMMY; // allow garbage collector to reclaim prev
                }
            }
            else {
                // fast path to fix head
                prev.next = first;
                casHead(first, prev);
                first.prev = DUMMY;
            }
        }
    }


    /**
     * Insert specified key into the linked list set.
     */
    public boolean insert(int key, int tid)
    {
        Node n = new Node(key, INSERT, tid);
        enlist(n, tid);
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
        Node n = new Node(key, INSERT, -1);
        n.next = head;
        head = n;
        boolean b = helpInsert(n, key);
        n.state = (b ? DATA : DEAD);
        return b;
    }

    /**
     * Remove specified key from the linked list set.
     */
    public boolean remove(int key, int tid)
    {
        Node n = new Node(key, REMOVE, tid);
        enlist(n, tid);
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
                    return (s == DATA) || (s == INSERT);
            }
            curr = curr.next;
        }
        return false;
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
