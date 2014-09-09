interface ISet
{
    boolean insert(int key, int tid);
    boolean remove(int key, int tid);
    boolean contains(int key);

    // for population uses
    boolean insert2(int key);
}
