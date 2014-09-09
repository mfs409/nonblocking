import java.util.Random;

class CheckingThread extends Thread
{
    private int id;
    private Random oprng;
    private Random keyrng;
    private boolean resize;
    private ISet set;

    public int [] numInsert = new int[SetBench.KEY_RANGE];
    public int [] numRemove = new int[SetBench.KEY_RANGE];

    public CheckingThread(int i, boolean resize)
    {
        this.id = i;
        this.oprng = new Random(i);
        this.keyrng = new Random(i);
        this.resize = resize;
        this.set = SetBench.set;
    }

    public void run()
    {
        while (!SetBench.begin);

        while (!SetBench.stop) {
            int op = oprng.nextInt(2);
            int key = keyrng.nextInt(SetBench.KEY_RANGE);

            if (op == 1) {
                if (insert(key, id))
                    numInsert[key]++;
                else if (remove(key, id))
                    numRemove[key]++;
            }
            else {
                if (remove(key, id))
                    numRemove[key]++;
                else if (insert(key, id))
                    numInsert[key]++;
            }
        }
    }

    private boolean insert(int key, int id)
    {
        if (resize)
            return set.insert(key, id);
        else
            return set.simpleInsert(key, id);
    }

    private boolean remove(int key, int id)
    {
        if (resize)
            return set.remove(key, id);
        else
            return set.simpleRemove(key, id);
    }
}
