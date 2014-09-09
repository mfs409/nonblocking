import java.util.concurrent.atomic.*;
import java.util.*;

class WFListHashSet implements ISet
{
    static class HNode
    {
        // points to old HNode
        public HNode old;

        // bucket array
        public AtomicReferenceArray<WFListFSet> buckets;

        // store the size [for convenience]
        public final int size;

        // constructor
        public HNode(HNode o, int s)
        {
            old = o;
            size = s;
            buckets = new AtomicReferenceArray<WFListFSet>(size);
        }
    }

    public final static int MIN_BUCKET_NUM = 1;
    public final static int MAX_BUCKET_NUM = 1 << 16;

    // points to the current hash table
    volatile HNode head;
    volatile long counter;
    private AtomicReferenceArray<WFListNode> A;

    // field updaters
    private static AtomicReferenceFieldUpdater<WFListHashSet, HNode> headUpdater
        = AtomicReferenceFieldUpdater.newUpdater(WFListHashSet.class, HNode.class, "head");
    private static AtomicLongFieldUpdater<WFListHashSet> counterUpdater
        = AtomicLongFieldUpdater.newUpdater(WFListHashSet.class, "counter");

    public WFListHashSet()
    {
        head = new HNode(null, MIN_BUCKET_NUM);
        head.buckets.set(0, new WFListFSet());
        counter = 0;
        A = new AtomicReferenceArray<WFListNode>(SetBench.THREAD_NUM);
        for (int i = 0; i < A.length(); i++) {
            WFListNode n = new WFListNode(-1, -1);
            n.priority = Long.MAX_VALUE;
            A.set(i, n);
        }
    }

    public boolean insert(int key, int tid)
    {
        HNode h = head;
        int result = apply(WFListNode.INSERT, key, tid);
        if (Math.abs(result) > 8)
            resize(h, true);
        return result > 0;
    }

    public boolean remove(int key, int tid)
    {
        int result = apply(WFListNode.REMOVE, key, tid);
        return result > 0;
    }

    public boolean contains(int key)
    {
        HNode t = head;
        WFListFSet b = t.buckets.get(key % t.size);
        // if the b is empty, use old table
        if (b == null) {
            HNode s = t.old;
            b = (s == null)
                ? t.buckets.get(key % t.size)
                : s.buckets.get(key % s.size);
        }
        return b.hasMember(key);
    }

    public boolean simpleInsert(int key, int tid)
    {
        return apply(WFListNode.INSERT, key, tid) > 0;
    }

    public boolean simpleRemove(int key, int tid)
    {
        return apply(WFListNode.REMOVE, key, tid) > 0;
    }

    public boolean grow()
    {
        HNode h = head;
        return resize(h, true);
    }

    public boolean shrink()
    {
        HNode h = head;
        return resize(h, false);
    }

    public int getBucketSize()
    {
        return head.size;
    }

    public void print()
    {
        HNode curr = head;
        int age = 0;
        while (curr != null) {
            System.out.println("HashTableNode #" + Integer.toString(age++));
            for (int i = 0; i < curr.size; i++) {
                System.out.print("  Bucket " + Integer.toString(i) + ": ");
                if (curr.buckets.get(i) != null)
                    curr.buckets.get(i).print();
                else
                    System.out.println();
            }
            curr = curr.old;
            System.out.println();
        }
    }

    private int apply(int type, int key, int mytid)
    {
        long prio = counterUpdater.getAndIncrement(this);
        WFListNode myop = new WFListNode(key, type);
        myop.priority = prio;
        A.set(mytid, myop);

        for (int tid = 0; tid < A.length(); tid++) {
            WFListNode op = A.get(tid);
            while (op.priority <= prio) {
                HNode      t = head;
                int        i = op.key % t.size;
                WFListFSet b = t.buckets.get(i);
                if (b == null)
                    helpResize(t, i);
                else if (b.invoke(op))
                    break;
            }
        }
        return WFListFSet.getResponse(myop);
    }

    private boolean resize(HNode t, boolean grow)
    {
        if ((t.size == MAX_BUCKET_NUM && grow) ||
            (t.size == MIN_BUCKET_NUM && !grow))
            return false;

        if (t == head) {
            // make sure we can deprecate t's predecessor
            for (int i = 0; i < t.size; i++) {
                if (t.buckets.get(i) == null)
                    helpResize(t, i);
            }
            // deprecate t's predecessor
            t.old = null;

            // switch to a new bucket array
            if (t == head) {
                HNode n = new HNode(t, grow ? t.size * 2 : t.size / 2);
                return casHead(t, n);
            }
        }
        return false;
    }

    private void helpResize(HNode t, int i)
    {
        WFListFSet b = t.buckets.get(i);
        HNode s = t.old;
        if (b == null && s != null) {
            WFListFSet set = null;
            if (s.size * 2 == t.size) /* growing */ {
                WFListFSet p = s.buckets.get(i % s.size);
                p.freeze();
                set = p.split(t.size, i);
            }
            else /* shrinking */ {
                WFListFSet p = s.buckets.get(i);
                WFListFSet q = s.buckets.get(i + t.size);
                p.freeze();
                q.freeze();
                set = p.merge(q);
            }
            t.buckets.compareAndSet(i, null, set);

        }
    }

    private boolean casHead(HNode o, HNode n)
    {
        return headUpdater.compareAndSet(this, o, n);
    }

}
