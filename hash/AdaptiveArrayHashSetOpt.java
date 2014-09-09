import java.util.concurrent.atomic.*;
import java.util.*;

class AdaptiveArrayHashSetOpt implements ISet
{
    class HNode
    {
        // points to old HNode
        public HNode old;

        // bucket array
        public AtomicReferenceArray<FSet> buckets;

        // fflags array
        public AtomicIntegerArray fflags;

        // store the size [for convenience]
        public final int size;

        // constructor
        public HNode(HNode o, int s)
        {
            old = o;
            size = s;
            buckets = new AtomicReferenceArray<FSet>(size);
            fflags = new AtomicIntegerArray(size);
        }

        private boolean casBucket(int i, FSet o, FSet n)
        {
            return buckets.compareAndSet(i, o, n);
        }

        public void printBucket(int i)
        {
            FSet h = buckets.get(i);

            if (h.immutable()) {
                System.out.print("(F) ");
            }

            for (int k : h.arr)
                System.out.print(Integer.toString(k) + " ");
            System.out.println();
        }
    }

    class HelpRecord
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
            curTid = (curTid + 1) % SetBench.THREAD_NUM;
            lastPhase = A.get(curTid).priority;
            nextCheck = HELPING_DELAY;
        }
    }

    private static class FSet
    {
        public int[] arr;
        public volatile WFArrayOp op;

        public FSet(int [] a)
        {
            arr = a;
        }

        private static final AtomicReferenceFieldUpdater<FSet, WFArrayOp> opUpdater
            = AtomicReferenceFieldUpdater.newUpdater(FSet.class, WFArrayOp.class, "op");

        public boolean casOp(WFArrayOp o, WFArrayOp n)
        {
            return opUpdater.compareAndSet(this, o, n);
        }

        public boolean immutable()
        {
            return op != null && op.type == WFArrayOp.FREEZE;
        }
    }

    public final static int MIN_BUCKET_NUM = 1;
    public final static int MAX_BUCKET_NUM = 1 << 16;

    private static final int MAX_TRIAL = 256;
    private static final int HELPING_DELAY = 128;

    // points to the current hash table
    volatile HNode head;
    volatile long counter;
    private AtomicReferenceArray<WFArrayOp> A;
    private HelpRecord [] helpRecords;

    // field updaters
    private static AtomicReferenceFieldUpdater<AdaptiveArrayHashSetOpt, HNode> headUpdater
        = AtomicReferenceFieldUpdater.newUpdater(AdaptiveArrayHashSetOpt.class, HNode.class, "head");
    private static AtomicLongFieldUpdater<AdaptiveArrayHashSetOpt> counterUpdater
        = AtomicLongFieldUpdater.newUpdater(AdaptiveArrayHashSetOpt.class, "counter");

    public AdaptiveArrayHashSetOpt()
    {
        head = new HNode(null, MIN_BUCKET_NUM);
        head.buckets.set(0, new FSet(new int[0]));
        counter = 0;
        A = new AtomicReferenceArray<WFArrayOp>(SetBench.THREAD_NUM);
        helpRecords = new HelpRecord[SetBench.THREAD_NUM];

        for (int i = 0; i < A.length(); i++) {
            WFArrayOp n = new WFArrayOp(-1, -1);
            n.priority = Long.MAX_VALUE;
            A.set(i, n);
            helpRecords[i] = new HelpRecord();
        }
    }

    public boolean insert(int key, int tid)
    {
        HNode h = head;
        int result = apply(WFArrayOp.INSERT, key, tid);
        if (Math.abs(result) > 2)
            resize(h, true);
        return result > 0;
    }

    public boolean remove(int key, int tid)
    {
        int result = apply(WFArrayOp.REMOVE, key, tid);
        return result > 0;
    }

    public boolean contains(int key)
    {
        HNode t = head;
        FSet b = t.buckets.get(key % t.size);
        // if the b is empty, use old table
        if (b == null) {
            HNode s = t.old;
            b = (s == null)
                ? t.buckets.get(key % t.size)
                : s.buckets.get(key % s.size);
        }
        return hasMember(b, key);
    }

    public boolean simpleInsert(int key, int tid)
    {
        return apply(WFArrayOp.INSERT, key, tid) > 0;
    }

    public boolean simpleRemove(int key, int tid)
    {
        return apply(WFArrayOp.REMOVE, key, tid) > 0;
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
                    curr.printBucket(i);
                else
                    System.out.println();
            }
            curr = curr.old;
            System.out.println();
        }
    }

    void helpIfNeeded(int tid)
    {
        HelpRecord rec = helpRecords[tid];
        if (rec.nextCheck-- == 0) {
            WFArrayOp op = A.get(rec.curTid);
            if (op.priority != Long.MAX_VALUE && op.priority == rec.lastPhase) {
                while (op.priority <= rec.lastPhase) {
                    HNode       t = head;
                    int         i = op.key % t.size;
                    FSet b = t.buckets.get(i);
                    if (b == null)
                        helpResize(t, i);
                    else if (invoke(t, i, op))
                        break;
                }
            }
            rec.reset();
        }
    }

    private int apply(int type, int key, int mytid)
    {
        helpIfNeeded(mytid);

        WFArrayOp myop = new WFArrayOp(key, type);

        int trial = 0;
        while (++trial < MAX_TRIAL) {
            HNode       t = head;
            int         i = myop.key % t.size;
            FSet        b = t.buckets.get(i);
            if (b == null)
                helpResize(t, i);
            else if (invoke(t, i, myop))
                return myop.resp;
        }

        return applySlow(myop, mytid);
    }


    private int applySlow(WFArrayOp myop, int mytid)
    {
        long prio = counterUpdater.getAndIncrement(this);
        myop.priority = prio;
        A.set(mytid, myop);

        for (int tid = 0; tid < A.length(); tid++) {
            WFArrayOp op = A.get(tid);
            while (op.priority <= prio) {
                HNode       t = head;
                int         i = op.key % t.size;
                FSet        b = t.buckets.get(i);
                if (b == null)
                    helpResize(t, i);
                else if (invoke(t, i, op))
                    break;
            }
        }
        return myop.resp;
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
        FSet   b = t.buckets.get(i);
        HNode  s = t.old;
        if (b == null && s != null) {
            int [] set = null;
            if (s.size * 2 == t.size) /* growing */ {
                int [] p = freezeBucket(s, i % s.size);
                set = split(p, t.size, i);
            }
            else /* shrinking */ {
                int [] p = freezeBucket(s, i);
                int [] q = freezeBucket(s, i + t.size);
                set = merge(p, q);
            }
            t.buckets.compareAndSet(i, null, new FSet(set));
        }
    }

    private boolean casHead(HNode o, HNode n)
    {
        return headUpdater.compareAndSet(this, o, n);
    }

    public boolean invoke(HNode t, int i, WFArrayOp op)
    {
        FSet set = t.buckets.get(i);
        while (!set.immutable() && op.priority != Long.MAX_VALUE) {
            if (t.fflags.get(i) == 1) {
                doFreeze(t, i);
                return op.priority == Long.MAX_VALUE;
            }
            WFArrayOp pred = set.op;
            if (pred == null) {
                if (op.priority != Long.MAX_VALUE) {
                    if (set.casOp(null, op)) {
                        helpFinish(t, i, set);
                        return true;
                    }
                }
            }
            else {
                helpFinish(t, i, set);
            }
            set = t.buckets.get(i);
        }
        return op.priority == Long.MAX_VALUE;
    }

    public int [] freezeBucket(HNode t, int i)
    {
        t.fflags.set(i, 1);
        return doFreeze(t, i);
    }

    private int [] doFreeze(HNode t, int i)
    {
        WFArrayOp h = new WFArrayOp(-1, WFArrayOp.FREEZE);
        FSet set = t.buckets.get(i);
        while (!set.immutable()) {
            WFArrayOp pred = set.op;
            if (pred == null) {
                if (set.casOp(null, h))
                    return set.arr;
            }
            else {
                helpFinish(t, i, set);
            }
            set = t.buckets.get(i);
        }
        return set.arr;
    }

    private void helpFinish(HNode t, int i, FSet set)
    {
        WFArrayOp op = set.op;
        if (op != null && op.type != WFArrayOp.FREEZE) {
            int [] n = (op.type == WFArrayOp.INSERT)
                ? arrayInsert(set.arr, op.key)
                : arrayRemove(set.arr, op.key);
            op.resp = (n == set.arr) ? -(n.length + 1) : (n.length + 1);
            op.priority = Long.MAX_VALUE;
            t.casBucket(i, set, new FSet(n));
        }
    }

    public boolean hasMember(FSet set, int key)
    {
        // must be aware of the linearized operation if exists
        WFArrayOp op = set.op;
        if (op != null && op.key == key) {
            // note that op cannot be a freeze node
            return op.type == WFArrayOp.INSERT;
        }

        return arrayContains(set.arr, key);
    }

    public int [] split(int [] o, int size, int remainder)
    {
        int count = 0;
        for (int i = 0; i < o.length; i++)
            if (o[i] % size == remainder)
                count++;
        int [] n = new int[count];
        int j = 0;
        for (int i = 0; i < o.length; i++) {
            if (o[i] % size == remainder)
                n[j++] = o[i];
        }
        return n;
    }

    public int [] merge(int [] p, int [] q)
    {
        int [] n = new int[p.length + q.length];
        int j = 0;
        for (int i = 0; i < p.length; i++)
            n[j++] = p[i];
        for (int i = 0; i < q.length; i++)
            n[j++] = q[i];
        return n;
    }

    private static boolean arrayContains(int [] o, int key)
    {
        for (int i = 0; i < o.length; i++) {
            if (o[i] == key)
                return true;
        }
        return false;
    }

    private static int [] arrayInsert(int [] o, int key)
    {
        if (arrayContains(o, key))
            return o;
        int [] n = new int[o.length + 1];
        for (int i = 0; i < o.length; i++)
            n[i] = o[i];
        n[n.length - 1] = key;
        return n;
    }

    private static int [] arrayRemove(int [] o, int key)
    {
        if (!arrayContains(o, key))
            return o;
        int [] n = new int[o.length - 1];
        int j = 0;
        for (int i = 0; i < o.length; i++) {
            if (o[i] != key)
                n[j++] = o[i];
        }
        return n;
    }


}
