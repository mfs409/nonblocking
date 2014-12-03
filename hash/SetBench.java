import java.util.Random;
import java.text.*;
import gnu.getopt.Getopt;

class SetBench
{
    public static int THREAD_NUM   = 1;
    public static int DURATION     = 1;
    public static int RO_RATIO     = 34;
    public static int KEY_RANGE    = 4096;
    public static int INIT_SIZE    = 1024;
    public static String ALG_NAME  = "LFList";
    public static boolean SANITY_MODE  = false;

    public static volatile ISet set = null;
    public static volatile boolean begin = false;
    public static volatile boolean stop = false;

    private static void printHelp()
    {
        System.out.println("  -a     algorithm");
        System.out.println("  -p     thread num");
        System.out.println("  -d     duration");
        System.out.println("  -R     lookup ratio (0~100)");
        System.out.println("  -M     key range");
        System.out.println("  -I     initial size");
        System.out.println("  -c     sanity mode");
    }

    private static boolean ParseArgs(String [] args)
    {
        Getopt g = new Getopt("", args, "a:p:d:R:M:I:hc");
        int c;
        String arg = null;
        while ((c = g.getopt()) != -1)
        {
            switch(c)
            {
              case 'a':
                ALG_NAME = g.getOptarg();
                break;
              case 'p':
                arg = g.getOptarg();
                THREAD_NUM = Integer.parseInt(arg);
                break;
              case 'd':
                arg = g.getOptarg();
                DURATION = Integer.parseInt(arg);
                break;
              case 'R':
                arg = g.getOptarg();
                RO_RATIO = Integer.parseInt(arg);
                break;
              case 'M':
                arg = g.getOptarg();
                KEY_RANGE = Integer.parseInt(arg);
                break;
              case 'I':
                arg = g.getOptarg();
                INIT_SIZE = Integer.parseInt(arg);
                break;
              case 'c':
                SANITY_MODE = true;
                break;
              case 'h':
                printHelp();
                return false;
              default:
                return false;
            }
        }
        return true;
    }

    private static void RunBench(boolean warmup)
        throws InterruptedException
    {
        CreateSet();

        Random rng = new Random();
        for (int i = 0; i < INIT_SIZE; i++) {
            while (true) {
                int key = rng.nextInt(KEY_RANGE);
                if (set.insert(key, 0))
                    break;
            }
        }

        begin = false;
        stop = false;

        BenchOpsThread [] threads = new BenchOpsThread[THREAD_NUM];

        for (int i = 0; i < threads.length; i++) {
            threads[i] = new BenchOpsThread(i);
            threads[i].start();
        }

        // record start time
        long startTime = System.currentTimeMillis();

        // broadcast begin signal
        begin = true;

        // sleep the main thread
        if (warmup)
            Thread.sleep(500);
        else
            Thread.sleep(DURATION * 1000);

        // broadcast stop signal
        stop = true;

        // wait until every thread finishes
        for (int i = 0; i < threads.length; i++) {
            threads[i].join();
        }

        // record end time
        long endTime = System.currentTimeMillis();

        // compute elapsed time
        long elapsed = endTime - startTime;

        long totalOps = 0;
        for (int i = 0; i < threads.length; i++) {
            totalOps += threads[i].ops;
        }

        if (!warmup) {
            System.out.print("Throughput(ops/ms): ");
            System.out.println(new DecimalFormat("#.##").format((double)totalOps / elapsed));
        }

        System.gc();
    }

    private static boolean SanityCheck(int numCheckingThread, int numResizingThread)
        throws InterruptedException
    {
        CreateSet();

        long [] totalInsert = new long[KEY_RANGE];
        long [] totalRemove = new long[KEY_RANGE];
        long totalGrow = 0;
        long totalShrink = 0;

        Random rng = new Random();
        for (int i = 0; i < INIT_SIZE; i++) {
            while (true) {
                int key = rng.nextInt(KEY_RANGE);
                if (set.insert(key, 0)) {
                    totalInsert[key]++;
                    break;
                }
            }
        }

        long initBucketSize = set.getBucketSize();

        begin = false;
        stop = false;

        CheckingThread [] cthreads  = new CheckingThread[numCheckingThread];
        ResizingThread [] rthreads = new ResizingThread[numResizingThread];

        for (int i = 0; i < cthreads.length; i++) {
            cthreads[i] = new CheckingThread(i, false);
            cthreads[i].start();
        }
        for (int i = 0; i < rthreads.length; i++) {
            rthreads[i] = new ResizingThread(i);
            rthreads[i].start();
        }

        // broadcast begin signal
        begin = true;

        // sleep the main thread
        Thread.sleep(DURATION * 1000);

        // broadcast stop signal
        stop = true;

        // wait until every thread finishes
        for (int i = 0; i < rthreads.length; i++) {
            rthreads[i].join();
        }
        for (int i = 0; i < cthreads.length; i++) {
            cthreads[i].join();
        }

        for (int i = 0; i < cthreads.length; i++) {
            for (int key = 0; key < KEY_RANGE; key++) {
                totalInsert[key] += cthreads[i].numInsert[key];
                totalRemove[key] += cthreads[i].numRemove[key];
            }
        }
        for (int i = 0; i < rthreads.length; i++) {
            totalGrow += rthreads[i].numGrow;
            totalShrink += rthreads[i].numShrink;
        }

        // check whether the final bucket array size match the number of effective
        // grow and shrink operations
        if (totalGrow >= totalShrink) {
            if (set.getBucketSize() != (initBucketSize << (totalGrow - totalShrink)))
                System.out.println("Sanity check: failed.");
        }
        else {
            if (set.getBucketSize() != (initBucketSize >> (totalShrink - totalGrow)))
                System.out.println("Sanity check: failed.");
        }

        // check set-membership of each key value matches the number of effective
        // insert and remove operations on that key
        for (int key = 0; key < KEY_RANGE; key++) {
            if (set.contains(key)) {
                if (totalInsert[key] != totalRemove[key] + 1) {
                    System.out.println("Key exist case..");
                    System.out.println(totalInsert[key]);
                    System.out.println(totalRemove[key]);
                    System.out.println("Sanity check: failed.");
                    return false;
                }
            }
            else {
                if (totalInsert[key] != totalRemove[key]) {
                    System.out.println("Key not exist case..");
                    System.out.println(totalInsert[key]);
                    System.out.println(totalRemove[key]);
                    System.out.println("Sanity check: failed.");
                    return false;
                }
            }
        }
        System.out.println("Sanity check: okay.");
        return true;
    }

    private static void CreateSet()
    {
        if (ALG_NAME.equals("LFList")) {
            SetBench.set = new LFListHashSet();
        }
        else if (ALG_NAME.equals("WFList")) {
            SetBench.set = new WFListHashSet();
        }
        else if (ALG_NAME.equals("LFArray")) {
            SetBench.set = new LFArrayHashSet();
        }
        else if (ALG_NAME.equals("LFArrayOpt")) {
            SetBench.set = new LFArrayHashSetOpt();
        }
        else if (ALG_NAME.equals("WFArray")) {
            SetBench.set = new WFArrayHashSet();
        }
        else if (ALG_NAME.equals("AdaptiveArray")) {
            SetBench.set = new AdaptiveArrayHashSet();
        }
        else if (ALG_NAME.equals("AdaptiveArrayOpt")) {
            SetBench.set = new AdaptiveArrayHashSetOpt();
        }
        else if (ALG_NAME.equals("SO")) {
            SetBench.set = new SOHashSet();
        }
        else if (ALG_NAME.equals("Std")) {
            SetBench.set = new ConcurrentHashSet();
        }
        else {
            SetBench.set = new LFListHashSet();
        }
    }


    public static void main(String[] args)
        throws InterruptedException
    {
        if (!ParseArgs(args))
            return;

        if (SANITY_MODE) {
            SanityCheck(1, THREAD_NUM);
            SanityCheck(THREAD_NUM, 1);
            System.gc();

        }
        else {
            RunBench(true);
            RunBench(false);
        }

        // //LFArrayHashSet h = new LFArrayHashSet();
        // SOHashSet h = new SOHashSet();
        // for (int i = 0; i < 100; i++) {
        //     h.insert(i, 0);
        // }
        // h.print();
    }
}
