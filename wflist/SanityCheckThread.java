import java.util.Random;

class SanityCheckThread extends Thread
{
    private int id;
    private Random oprng;
    private Random keyrng;

    public int [] numInsert = new int[ListBench.KEY_RANGE];
    public int [] numRemove = new int[ListBench.KEY_RANGE];

    public SanityCheckThread(int i)
    {
        this.id = i;
        this.oprng = new Random(i);
        this.keyrng = new Random(i);
    }

    public void run()
    {
        ISet set = ListBench.set;

        while (!ListBench.begin);

        while (!ListBench.stop) {
            int op = oprng.nextInt(2);
            int key = keyrng.nextInt(ListBench.KEY_RANGE);

            if (op == 1) {
                if (set.insert(key, id))
                    numInsert[key]++;
                else if (set.remove(key, id))
                    numRemove[key]++;
            }
            else {
                if (set.remove(key, id))
                    numRemove[key]++;
                else if (set.insert(key, id))
                    numInsert[key]++;
            }
        }
    }
}
