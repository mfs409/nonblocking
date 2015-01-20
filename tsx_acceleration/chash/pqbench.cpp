#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <atomic>
#include <cstring>
#include <queue>
#include <unistd.h>

#include "alt-license/rand_r_32.h"
#include "mm.hpp"

#include "mound.hpp"
#include "mound_htm.hpp"
#include "mound_htmff.hpp"
#include "slpq.hpp"
#include "slpq_htm.hpp"
#include "slpq_htmff.hpp"

using namespace std;

static const int32_t PQ_VAL_MAX = std::numeric_limits<int32_t>::max();

static uint32_t NUM_THREADS  = 1;
static uint32_t DURATION     = 1;
static uint32_t KEY_RANGE    = PQ_VAL_MAX;
static uint32_t INIT_SIZE    = 65536;
static uint32_t DELAY        = 0;
static string ALG_NAME  = "";
static bool SANITY_MODE = false;

static std::atomic<bool> bench_begin;
static std::atomic<bool> bench_stop;

static void printHelp()
{
    cout << "  -a     algorithm" << endl;
    cout << "  -p     thread num" << endl;
    cout << "  -d     duration" << endl;
    cout << "  -M     key range" << endl;
    cout << "  -I     initial size" << endl;
    cout << "  -l     delay" << endl;
    cout << "  -c     sanity mode" << endl;
}

static bool parseArgs(int argc, char** argv)
{
    int c;
    while ((c = getopt(argc, argv, "a:p:d:M:I:l:hc")) != -1)
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
          case 'M':
            KEY_RANGE = atoi(optarg);
            break;
          case 'l':
            DELAY = atoi(optarg);
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

template<class PQ>
void benchOpsThread(bench_ops_thread_arg_t * arg)
{
    wbmm_thread_init(arg->tid);

    uint32_t seed1 = arg->tid;
    uint32_t seed2 = seed1 + 1;

    uint64_t ops = 0;
    PQ * set = (PQ *)arg->set;

    while (!bench_begin);

    while (!bench_stop) {
        int op  = rand_r_32(&seed1) % 100;
        int key = rand_r_32(&seed2) % KEY_RANGE;
        if (op < 50) {
            set->add(key);
        }
        else {
            key = set->remove();
        }
        for (int i = 0; i < DELAY; i++) spin64();
        ops++;
    }
    arg->ops = ops;
}

template<class PQ>
static void runBench()
{
    PQ set;

    uint32_t seed = 0;
    for (uint32_t i = 0; i < INIT_SIZE; i++) {
        int key = rand_r_32(&seed) % KEY_RANGE;
        set.add(key);
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
        thrs[j] = new thread(benchOpsThread<PQ>, &arg);
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


struct sanity_thread_arg_t
{
    uintptr_t tid;
    void *    set;
    uint64_t  ops;
};

template<class PQ>
void sanityThread(sanity_thread_arg_t * arg)
{
    wbmm_thread_init(arg->tid);

    uint32_t seed1 = arg->tid;
    uint32_t seed2 = seed1 + 1;

    uint64_t ops = 0;
    PQ * set = (PQ *)arg->set;

    while (!bench_begin);

    while (!bench_stop) {
        int key = rand_r_32(&seed2) % KEY_RANGE;
        set->add(key);
        key = set->remove();
        ops++;
    }
    arg->ops = ops;
}

template<class PQ>
static bool sanityCheck()
{
    PQ set;

    uint32_t seed = 0;
    for (uint32_t i = 0; i < INIT_SIZE; i++) {
        int key = rand_r_32(&seed) % KEY_RANGE;
        set.add(key);
    }

    bench_begin = false;
    bench_stop = false;

    thread *              thrs[NUM_THREADS];
    sanity_thread_arg_t   args[NUM_THREADS];

    for (uint32_t j = 0; j < NUM_THREADS; j++) {
        sanity_thread_arg_t & arg = args[j];
        arg.tid = j + 1;
        arg.set = &set;
        arg.ops = 0;
        thrs[j] = new thread(sanityThread<PQ>, &arg);
    }

    // broadcast begin signal
    bench_begin = true;
    sleep(DURATION);
    bench_stop = true;

    for (uint32_t j = 0; j < NUM_THREADS; j++) thrs[j]->join();

    uint32_t old = 0;
    for (uint32_t i = 0; i < INIT_SIZE; i++) {
        uint32_t num = set.remove();
        if (old > num) {
            cout << "error: heap invariant violated: "
                      << "prev = " << old << " "
                      << "curr = " << num << endl;
            return false;
        }
        if (num == PQ_VAL_MAX) {
            cout << "error: missing element(not linearizable)" << endl;
            return false;
        }
        old = num;
    }
    uint32_t num = set.remove();
    if (num != PQ_VAL_MAX) {
        cout << "error: extra element(not linearizable)" << endl;
        return false;
    }
    cout << "Sanity check: okay." << endl;
    return true;
}

struct pqcompare {
    bool operator() (const int& l, const int& r) { return l > r; }
};

template<class PQ>
bool sanityCheckSequential()
{
    const int max = 10000;
    std::priority_queue<int32_t, vector<int32_t>, pqcompare> contrast;
    PQ m;
    uint32_t seed = 0;
    for (int i = 0; i < max; i++) {
        int32_t temp = rand_r_32(&seed) % KEY_RANGE;
        contrast.push(temp);
        m.add(temp);
    }
    for (int i = 0; i < max - 1; i++) {
        uint32_t r1 = m.remove();
        uint32_t r2 = contrast.empty() ? PQ_VAL_MAX : contrast.top();
        if (!contrast.empty()) contrast.pop();
        if (r1 != r2) {
            cout << "different element at index " << i << ":"
                 << r1 << " " << r2 <<  endl;
            return false;
        }
    }
    cout << "Sanity check: okay." << endl;
    return true;
}

template<typename PQ>
void run()
{
    if (SANITY_MODE) {
        sanityCheckSequential<PQ>();
        sanityCheck<PQ>();
    }
    else
        runBench<PQ>();
}

int main(int argc, char** argv)
{
    if (!parseArgs(argc, argv)) {
        return 0;
    }

    wbmm_init(NUM_THREADS + 1);
    wbmm_thread_init(0);

    if (ALG_NAME == "Mound")
        run<moundpq_t>();
    else if (ALG_NAME == "MoundHTM")
        run<moundpq_htm_t>();
    else if (ALG_NAME == "MoundHTMFF")
        run<moundpq_htmff_t>();
    else if (ALG_NAME == "Skip")
        run<slpq_t>();
    else if (ALG_NAME == "SkipHTM")
        run<slpq_htm_t>();
    else if (ALG_NAME == "SkipHTMFF")
        run<slpq_htmff_t>();
    else {
        cout << "Algorithm not found." << endl;
    }

    return 0;
}
