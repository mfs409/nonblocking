#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <atomic>
#include <cstring>
#include <unistd.h>

#include "alt-license/rand_r_32.h"
#include "mm.hpp"

#include "hash.hpp"
#include "hash_cptr.hpp"
#include "hash_htm.hpp"
#include "hash_inplace.hpp"
#include "bst.hpp"
#include "bst_cptr.hpp"
#include "bst_htm1.hpp"
#include "bst_htm1ff.hpp"
#include "bst_htm2.hpp"
#include "bst_htm2ff.hpp"
#include "bst_htm3.hpp"
#include "bst_htm3ff.hpp"
#include "skip.hpp"
#include "skip_htm.hpp"
#include "skip_htmff.hpp"

using namespace std;

static uint32_t NUM_THREADS  = 1;
static uint32_t DURATION     = 1;
static uint32_t RO_RATIO     = 34;
static uint32_t KEY_RANGE    = 4096;
static uint32_t INIT_SIZE    = 1024;
static string ALG_NAME  = "BST";
static bool SANITY_MODE = false;

static std::atomic<bool> bench_begin;
static std::atomic<bool> bench_stop;

static void printHelp()
{
    cout << "  -a     algorithm" << endl;
    cout << "  -p     thread num" << endl;
    cout << "  -d     duration" << endl;
    cout << "  -R     lookup ratio (0~100)" << endl;
    cout << "  -M     key range" << endl;
    cout << "  -I     initial size" << endl;
    cout << "  -c     sanity mode" << endl;
}

static bool parseArgs(int argc, char** argv)
{
    int c;
    while ((c = getopt(argc, argv, "a:p:d:R:M:I:hc")) != -1)
    {
        switch(c)
        {
          case 'a':
            ALG_NAME = string(optarg);
            break;
          case 'p':
            NUM_THREADS = atoi(optarg);
            break;
          case 'd':
            DURATION = atoi(optarg);
            break;
          case 'R':
            RO_RATIO = atoi(optarg);
            break;
          case 'M':
            KEY_RANGE = atoi(optarg);
            break;
          case 'I':
            INIT_SIZE = atoi(optarg);
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

struct bench_ops_thread_arg_t
{
    uintptr_t tid;
    void *    set;
    uint64_t  ops;
};

template<class SET>
void benchOpsThread(bench_ops_thread_arg_t * arg)
{
    wbmm_thread_init(arg->tid);

    int cRatio = RO_RATIO;
    int iRatio = cRatio + (100 - cRatio) / 2;

    uint32_t seed1 = arg->tid;
    uint32_t seed2 = seed1 + 1;

    uint64_t ops = 0;
    SET * set = (SET *)arg->set;

    while (!bench_begin);

    while (!bench_stop) {
        int op  = rand_r_32(&seed1) % 100;
        int key = rand_r_32(&seed2) % KEY_RANGE;
        if (op < cRatio) {
            set->contains(key);
        }
        else if (op < iRatio) {
            set->insert(key);
        }
        else {
            set->remove(key);
        }
        ops++;
    }
    arg->ops = ops;
}

template<class SET>
static void runBench()
{
    SET set;

    uint32_t seed = 0;
    for (uint32_t i = 0; i < INIT_SIZE; i++) {
        while (true) {
            int key = rand_r_32(&seed) % KEY_RANGE;
            if (set.insert(key)) break;
        }
    }

    bench_begin = false;
    bench_stop = false;

    thread *                 thrs[NUM_THREADS];
    bench_ops_thread_arg_t   args[NUM_THREADS];

    for (uint32_t j = 0; j < NUM_THREADS; j++) {
        bench_ops_thread_arg_t & arg = args[j];
        arg.tid = j + 1;
        arg.set = &set;
        arg.ops = 0;
        thrs[j] = new thread(benchOpsThread<SET>, &arg);
    }

    // broadcast begin signal
    bench_begin = true;

    sleep(DURATION);

    bench_stop = true;

    for (uint32_t j = 0; j < NUM_THREADS; j++)
        thrs[j]->join();

    uint64_t totalOps = 0;
    for (uint32_t j = 0; j < NUM_THREADS; j++) {
        totalOps += args[j].ops;
    }

    cout << ("Throughput(ops/ms): ")
         << std::setprecision(6)
         << (double)totalOps / DURATION / 1000 << endl;
}

struct chk_thread_arg_t
{
    uintptr_t  tid;
    void *     set;
    uint32_t * numInsert;
    uint32_t * numRemove;
};

struct rsz_thread_arg_t
{
    uintptr_t  tid;
    void *     set;
    uint32_t   numGrow;
    uint32_t   numShrink;
};

template<class SET>
void checkingThread(chk_thread_arg_t * arg)
{
    wbmm_thread_init(arg->tid);

    uint32_t seed = arg->tid;
    SET * set = (SET *)arg->set;

    while (!bench_begin);

    while (!bench_stop) {
        int key = rand_r_32(&seed) % KEY_RANGE;
        if (set->contains(key)) {
            if (set->remove(key)) {
                arg->numRemove[key]++;
            }
        }
        else {
            if (set->insert(key)) {
                arg->numInsert[key]++;
            }
        }
    }
}

template<class SET>
void resizingThread(rsz_thread_arg_t * arg)
{
    wbmm_thread_init(arg->tid);

    uint32_t seed = arg->tid;
    SET * set = (SET *)arg->set;

    while (!bench_begin);

    while (!bench_stop) {
        if (rand_r_32(&seed) % 2 == 0) {
            if (set->grow()) {
                arg->numGrow++;
            }
        }
        else {
            if (set->shrink()) {
                arg->numShrink++;
            }
        }
    }
}

template<class SET>
static bool sanityCheck(uint32_t numCheckingThread, uint32_t numResizingThread)
{
    SET set;

    uint64_t * totalInsert = new uint64_t[KEY_RANGE];
    uint64_t * totalRemove = new uint64_t[KEY_RANGE];
    std::memset(totalInsert, 0, sizeof(uint64_t) * KEY_RANGE);
    std::memset(totalRemove, 0, sizeof(uint64_t) * KEY_RANGE);

    uint32_t seed = 0;
    for (uint32_t i = 0; i < INIT_SIZE; i++) {
        while (true) {
            int key = rand_r_32(&seed) % KEY_RANGE;
            if (set.insert(key)) {
                totalInsert[key]++;
                break;
            }
        }
    }

    bench_begin = false;
    bench_stop = false;

    thread *           cthrs[numCheckingThread];
    thread *           rthrs[numResizingThread];
    chk_thread_arg_t   cargs[numCheckingThread];
    rsz_thread_arg_t   rargs[numResizingThread];

    for (uint32_t j = 0; j < numCheckingThread; j++) {
        chk_thread_arg_t & arg = cargs[j];
        arg.tid = j + 1;
        arg.set = &set;
        arg.numInsert = new uint32_t[KEY_RANGE];
        arg.numRemove = new uint32_t[KEY_RANGE];
        std::memset(arg.numInsert, 0, sizeof(uint32_t) * KEY_RANGE);
        std::memset(arg.numRemove, 0, sizeof(uint32_t) * KEY_RANGE);
        cthrs[j] = new thread(checkingThread<SET>, &arg);
    }
    for (uint32_t j = 0; j < numResizingThread; j++) {
        rsz_thread_arg_t & arg = rargs[j];
        arg.tid = numCheckingThread + j + 1;
        arg.set = &set;
        arg.numGrow = 0;
        arg.numShrink = 0;
        rthrs[j] = new thread(resizingThread<SET>, &arg);
    }

    // broadcast begin signal
    bench_begin = true;

    sleep(DURATION);

    // broadcast stop signal
    bench_stop = true;

    // join threads
    for (uint32_t j = 0; j < numCheckingThread; j++)
        cthrs[j]->join();
    for (uint32_t j = 0; j < numResizingThread; j++)
        rthrs[j]->join();

    // collect per-thread # of ops
    for (uint32_t i = 0; i < numCheckingThread; i++) {
        for (uint32_t key = 0; key < KEY_RANGE; key++) {
            totalInsert[key] += cargs[i].numInsert[key];
            totalRemove[key] += cargs[i].numRemove[key];
        }
    }

    // free threads & args
    for (uint32_t j = 0; j < numCheckingThread; j++) {
        chk_thread_arg_t & arg = cargs[j];
        delete [] arg.numInsert;
        delete [] arg.numRemove;
        delete cthrs[j];
    }
    for (uint32_t j = 0; j < numResizingThread; j++) {
        delete rthrs[j];
    }

    // check set-membership of each key value matches the number of effective
    // insert and remove operations on that key
    for (uint32_t key = 0; key < KEY_RANGE; key++) {
        if (set.contains(key)) {
            if (totalInsert[key] != totalRemove[key] + 1) {
                cout << "Key exist.." << endl;
                cout << totalInsert[key] << endl;
                cout << totalRemove[key] << endl;
                cout << "Sanity check: failed." << endl;
                return false;
            }
        }
        else {
            if (totalInsert[key] != totalRemove[key]) {
                cout << "Key not exist.." << endl;
                cout << totalInsert[key] << endl;
                cout << totalRemove[key] << endl;
                cout << "Sanity check: failed." << endl;
                return false;
            }
        }
    }
    cout << "Sanity check: okay." << endl;
    return true;
}

template<typename SET>
void run()
{
    if (SANITY_MODE) {
        sanityCheck<SET>(1, NUM_THREADS);
        sanityCheck<SET>(NUM_THREADS, 1);
    }
    else
        runBench<SET>();
}

int main(int argc, char** argv)
{
    if (!parseArgs(argc, argv)) {
        return 0;
    }

    wbmm_init(NUM_THREADS + 2);
    wbmm_thread_init(0);

    if (ALG_NAME == "Tree")
        run<bstset_t>();
    else if (ALG_NAME == "TreeHTM1")
        run<bstset_htm1_t>();
    else if (ALG_NAME == "TreeHTM1FF")
        run<bstset_htm1ff_t>();
    else if (ALG_NAME == "TreeHTM2")
        run<bstset_htm2_t>();
    else if (ALG_NAME == "TreeHTM2FF")
        run<bstset_htm2ff_t>();
    else if (ALG_NAME == "TreeHTM3")
        run<bstset_htm3_t>();
    else if (ALG_NAME == "TreeHTM3FF")
        run<bstset_htm3ff_t>();
    else if (ALG_NAME == "TreeCPTR")
        run<bstset_cptr_t>();
    else if (ALG_NAME == "Hash")
        run<hashset_t>();
    else if (ALG_NAME == "HashHTM")
        run<hashset_htm_t>();
    else if (ALG_NAME == "HashInplace")
        run<hashset_inplace_t>();
    else if (ALG_NAME == "HashCPTR")
        run<hashset_cptr_t>();
    else if (ALG_NAME == "Skip")
        run<slset_t>();
    else if (ALG_NAME == "SkipHTM")
        run<slset_htm_t>();
    else if (ALG_NAME == "SkipHTMFF")
        run<slset_htmff_t>();
    else {
        cout << "Algorithm not found." << endl;
    }

    return 0;
}
