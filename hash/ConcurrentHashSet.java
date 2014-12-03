import java.util.concurrent.*;
import java.util.*;

class ConcurrentHashSet implements ISet
{
    private Set<Integer> set;

    public ConcurrentHashSet()
    {
        set = Collections.newSetFromMap(new ConcurrentHashMap<Integer, Boolean>());
    }


    public boolean insert(int key, int tid)
    {
        return set.add(key);
    }

    public boolean remove(int key, int tid)
    {
        return set.remove(key);
    }

    public boolean contains(int key)
    {
        return set.contains(key);
    }


    public boolean simpleInsert(int key, int tid)
    {
        return set.add(key);
    }

    public boolean simpleRemove(int key, int tid)
    {
        return set.remove(key);
    }

    public boolean grow()
    {
        return false;
    }

    public boolean shrink()
    {
        return false;
    }

    public int getBucketSize()
    {
        return 1;
    }
}
