import java.util.Random;

class BenchOpsThread extends Thread
{
    private int id;
    private Random oprng;
    private Random keyrng;

    public int ops = 0;

    public BenchOpsThread(int i)
    {
        this.id = i;
        this.oprng = new Random(i);
        this.keyrng = new Random(i);
    }

    public void run()
    {
        int cRatio = ListBench.RO_RATIO;
        int iRatio = cRatio + (100 - cRatio) / 2;
        ISet set = ListBench.set;

        while (!ListBench.begin);

        while (!ListBench.stop) {
            int op = oprng.nextInt(100);
            int key = keyrng.nextInt(ListBench.KEY_RANGE);

            if (op < cRatio) {
                set.contains(key);
            }
            else if (op < iRatio) {
                set.insert(key, id);
            }
            else {
                set.remove(key, id);
            }

            ops++;
        }
    }
}
