///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2010
// Lehigh University
// Computer Science and Engineering Department
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the University of Rochester nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


#include <string>
#include <iostream>
#include <pthread.h>
#include <stdlib.h>
#include <limits>
#include "../common/platform.hpp"
#include "../common/locks.hpp"
#include "Mindicator.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using namespace mindicator;

const int RANGE_MAX = 1024;
int SLEEP_TIME = 2;
int RANDKEY_THREADS = 0; // "visitors"
int UNIQUEKEY_THREADS = 0; // "samplers"
int QUERY_THREADS = 4;
bool PRINT_SUMMARY = false;
bool TEST_LINEARIZABILITY = false;
bool BENCH_MODE = true;

template<class SOSI>
struct sosil_querier_thread_args_t
{
    SOSI* sosi;
    uint32_t   num_visit;
};

template<class SOSI>
struct sosil_visitor_thread_args_t
{
    int   id;
    SOSI* sosi;
    uint32_t   num_visit;
    uint32_t   num_error;
    unsigned int   seed;
};

template<class SOSI>
struct sosil_sampler_thread_args_t
{
    int   id;
    int   seed;
    SOSI* sosi;
    uint32_t   num_visit;
    uint32_t   num_crown;
    uint32_t   num_error;
};

volatile static bool sosil_concurrent_test_flag = false;


template <class SOSI>
static void* sosil_querier(void* arg)
{
    sosil_querier_thread_args_t<SOSI>* v =
        (sosil_querier_thread_args_t<SOSI>*)arg;
    SOSI & sosi = *v->sosi;

    while (!sosil_concurrent_test_flag) {
        sosi.query();
        v->num_visit++;
    }

    return 0;
}

/**
 * A client periodically invoke arrive and depart on its sosi node.
 */
template <class SOSI>
static void* sosil_visitor(void* arg)
{
    sosil_visitor_thread_args_t<SOSI>* v =
        (sosil_visitor_thread_args_t<SOSI>*)arg;
    SOSI & sosi = *v->sosi;

    while (!sosil_concurrent_test_flag) {
    //while(v->num_visit < 10){
        // generate timestamp
        int ts = rand_r(&v->seed) % RANGE_MAX + 1;

        //for(volatile double i = 1; i < 25 ; i ++);

        // arrive
        sosi.arrive(v->id, ts);


        // sanity check
        int32_t oldest = sosi.query();
        if (ts < oldest) {
            v->num_error++;
        }

        //for(volatile double i = 1; i < 25 ; i ++);

        // depart
        sosi.depart(v->id);

        // increment visit number
        v->num_visit++;
    }

    return 0;
}

/**
 * A sampler periodically invoke arrive and depart on its sosi node with
 * fixed seed. It records the "crown number" that the last arrive become
 * the oldest sampler. Intuitively, smaller seed has higher chance to
 * become the oldest sampler.
 */
template<class SOSI>
static void * sosil_sampler(void *arg)
{
    sosil_sampler_thread_args_t<SOSI> *v = (sosil_sampler_thread_args_t<SOSI> *)arg;
    SOSI & sosi = *v->sosi;

    while (!sosil_concurrent_test_flag) {
        // arrive
        sosi.arrive(v->id, v->seed);

        // does the last visit become the oldest sampler?
        int32_t oldest = sosi.query();
        if (v->seed == oldest)
            v->num_crown++;
        // sanity check
        else if (v->seed < oldest)
            v->num_error++;

        // depart
        sosi.depart(v->id);

        if (TEST_LINEARIZABILITY)
            if (sosi.query() == v->seed) {
                //printf("%d %d\n", sosi.query(), v->seed);
                v->num_error++;
            }

        // increment visit number
        v->num_visit++;
    }

    return 0;
}

/**
 * Concurrent test instantiate several clients, each client periodically
 * access its sosi node.
 */
template<class SOSI>
static void sosil_concurrent_test()
{
    sosil_visitor_thread_args_t<SOSI> args1[RANDKEY_THREADS];
    sosil_sampler_thread_args_t<SOSI> args2[UNIQUEKEY_THREADS];
    pthread_t tid1[RANDKEY_THREADS];
    pthread_t tid2[UNIQUEKEY_THREADS];

    SOSI s;

    for (int j = 0; j < RANDKEY_THREADS; j++) {
        args1[j].id = j;
        args1[j].sosi = &s;
        args1[j].num_visit = 0;
        args1[j].num_error = 0;
        args1[j].seed = j; // NB: each thread gets its own seed, but the same
                           // seeds are used every time.
    }

    int seed = 1;
    for (int j = 0; j < UNIQUEKEY_THREADS; j++) {
        args2[j].id = j + RANDKEY_THREADS;
        args2[j].sosi = &s;
        args2[j].num_visit = 0;
        args2[j].num_crown = 0;
        args2[j].num_error = 0;
        args2[j].seed = seed;
        seed += RANGE_MAX / UNIQUEKEY_THREADS;
    }

    int32_t initial = s.query();

    srand(time(NULL));
    sosil_concurrent_test_flag = false;

    for (int j = 0; j < RANDKEY_THREADS; j++)
        pthread_create(&tid1[j], NULL, &sosil_visitor<SOSI>, &args1[j]);
    for (int j = 0; j < UNIQUEKEY_THREADS; j++)
        pthread_create(&tid2[j], NULL, &sosil_sampler<SOSI>, &args2[j]);

    sleep(SLEEP_TIME);
    sosil_concurrent_test_flag = true;

    for (int j = 0; j < RANDKEY_THREADS; j++)
        pthread_join(tid1[j], NULL);
    for (int j = 0; j < UNIQUEKEY_THREADS; j++)
        pthread_join(tid2[j], NULL);

    // total number of visit
    int total_visit = 0;

    for (int j = 0; j < RANDKEY_THREADS; j++) {
        total_visit += args1[j].num_visit;

        // sanity check for visitor j
        if (args1[j].num_error != 0)
            cout << "  Sanity check failed for visitor " << j << ": "
                 << "num_error = " << args1[j].num_error << endl;
    }

    for (int j = 0; j < UNIQUEKEY_THREADS; j++) {
        total_visit += args2[j].num_visit;

        // sanity check for sampler j
        if (args2[j].num_error != 0)
            cout << "  Sanity check failed for sampler " << j << ": "
                 << "num_error = " << args2[j].num_error << endl;
    }

    cout << " Throughput = " << total_visit / SLEEP_TIME << endl;

    // thread 0's crown rate should be 100%
    if (UNIQUEKEY_THREADS != 0 && args2[0].num_crown != args2[0].num_visit)
        cout << "  Sanity check failed: Thread 0 crown rate = "
             << (double)args2[0].num_crown / args2[0].num_visit * 100 << "%"
             << endl;

    // the oldest should be max when all threads left
    int32_t final = s.query();
    if (initial != final)
        cout << "  Sanity check failed: I(initial) = "<< initial
             << ", I(final) = " << final << endl;

    // print summary of each visitor and sampler
    if (PRINT_SUMMARY) {
        for (int j = 0; j < RANDKEY_THREADS; j++) {
            cout << "  Visitor " << j << ": "
                 << "num_visit = " << args1[j].num_visit << endl;
        }
        for (int j = 0; j < UNIQUEKEY_THREADS; j++) {
            cout << "  Sampler " << j << ": "
                 << "num_crown = " << args2[j].num_crown << ", "
                 << "num_visit = " << args2[j].num_visit << endl;
        }
    }
}

template<class SOSI>
static void sosil_bench()
{
    sosil_visitor_thread_args_t<SOSI> args1[RANDKEY_THREADS];
    pthread_t tid1[RANDKEY_THREADS];
    sosil_querier_thread_args_t<SOSI> args2[QUERY_THREADS];
    pthread_t tid2[QUERY_THREADS];

    SOSI s;

    for (int j = 0; j < RANDKEY_THREADS; j++) {
        args1[j].id = j;
        args1[j].sosi = &s;
        args1[j].num_visit = 0;
        args1[j].num_error = 0;
        args1[j].seed = j; // NB: each thread gets its own seed, but the same
                           // seeds are used every time.
    }

    for (int j = 0; j < QUERY_THREADS; j++) {
        args2[j].sosi = &s;
        args2[j].num_visit = 0;
    }

    srand(time(NULL));
    sosil_concurrent_test_flag = false;

    for (int j = 0; j < RANDKEY_THREADS; j++)
        pthread_create(&tid1[j], NULL, &sosil_visitor<SOSI>, &args1[j]);

    if (QUERY_THREADS > 0)
        for (int j = 0; j < QUERY_THREADS; j++)
            pthread_create(&tid2[j], NULL, &sosil_querier<SOSI>, &args2[j]);

    sleep(SLEEP_TIME);
    sosil_concurrent_test_flag = true;

    for (int j = 0; j < RANDKEY_THREADS; j++)
        pthread_join(tid1[j], NULL);

    if (QUERY_THREADS > 0)
        for (int j = 0; j < QUERY_THREADS; j++)
            pthread_join(tid2[j], NULL);

    uint64_t total_visit = 0;
    uint64_t total_query = 0;
    for (int j = 0; j < RANDKEY_THREADS; j++)
        total_visit += args1[j].num_visit;
    for (int j = 0; j < QUERY_THREADS; j++)
        total_query += args2[j].num_visit;

    cout << total_visit / SLEEP_TIME << endl;

    if (QUERY_THREADS > 0)
        cout << "\n" << total_query / SLEEP_TIME / QUERY_THREADS;
}

template<class SOSI>
static void run()
{
    if (BENCH_MODE)
        sosil_bench < SOSI > ();
    else
        sosil_concurrent_test < SOSI > ();
}

void usage()
{
    cout << "Command Line Options:" << endl
         << "  -h     :  print help" << endl
         << "  no args: run all tests" << endl
         << "  -Z     : run all tests" << endl
         << "  -b     : disable benchmark mode" << endl
         << "  -p     : thread number (benchmark mode only)" << endl
         << "  -q [Q] : query thread number (benchmark mode only)" << endl
         << "  -l     : run linearizable test (must pair with -t)" << endl
         << "  -t [T] : run test for SOSI given by name T" << endl
         << "  -p     : print detailed output" << endl
         << "  -d [D] : run each experiment for D seconds" << endl << endl
         << "Valid values for T:" << endl
         << "  List      : CGL DList implementation" << endl
         << "  SkipList  : SkipList implementation" << endl
         << "  LockMin   : FGL Tree, minimal summary" << endl
         << "  LockCache : FGL Tree, full summary" << endl
         << "  L64       : Linearizable, 32-bit vals" << endl
         << "  Q64       : Quiescent Consistency, 32-bit vals" << endl
         << "  XL64      : Linearizable, 32-bit vals, Arrive-Everywhere" << endl
         << "  XQ64      : Quiescent Consistency, 32-bit vals, Arrive-Everywhere" << endl
         << "  W64       : Wait-free, 16-bit vals" << endl
         << "  fArray    : fArray implementation" << endl
         << "  RTM    : RTM + lock free algorithm" << endl
         << "  RTM_cgl    : RTM + coarse grined lock" << endl
         << "  RTM_fgl    : RTM + fine grined lock" << endl
         << "  cgl    : coarse-grined lock" << endl;

    exit(-1);
}

struct config_t
{
    bool do_default;
    bool bench_mode;
    int  threads;
    int  query_threads;
    bool linearizable;
    string whichtest;
    config_t() : do_default(true), bench_mode(true), threads(1), query_threads(0),
                 linearizable(false), whichtest("") { }
} CONFIG;

int main(int argc, char** argv)
{
    // parse the command-line options

    int opt;
    while ((opt = getopt(argc, argv, "hblvt:d:p:q:Z")) != -1) {
        switch (opt) {
          case 'd':
            SLEEP_TIME = atoi(optarg);
            break;
          case 'h':
            usage();
            break;
          case 'b':
            CONFIG.bench_mode = false;
            break;
          case 'p':
            CONFIG.threads = atoi(optarg);
            break;
          case 'q':
            CONFIG.query_threads = atoi(optarg);
            break;
          case 'l':
            CONFIG.linearizable = true;
            break;
          case 'Z':
            CONFIG.do_default = true;
            break;
          case 't':
            CONFIG.do_default = false;
            CONFIG.whichtest = string(optarg);
            break;
          case 'v':
            PRINT_SUMMARY = true;
            break;
        }
    }
    BENCH_MODE = CONFIG.bench_mode;
    QUERY_THREADS = CONFIG.query_threads;
    TEST_LINEARIZABILITY = CONFIG.linearizable;

    if (BENCH_MODE) {
        RANDKEY_THREADS = CONFIG.threads;
        UNIQUEKEY_THREADS = 0;
    }
    else if (TEST_LINEARIZABILITY) {
        RANDKEY_THREADS = 0;
        UNIQUEKEY_THREADS = CONFIG.threads;
    }
    else {
        RANDKEY_THREADS = CONFIG.threads;
        UNIQUEKEY_THREADS = 0;
    }

    if (CONFIG.do_default) {
        CONFIG.whichtest = "L64";
    }

    cout << CONFIG.whichtest << ", " << CONFIG.threads << ", ";

    if (CONFIG.whichtest == "List") {
        run<sosillc_t> ();
    }
    else if (CONFIG.whichtest == "SkipList") {
        run<skiplist_t> ();
    }
    else if (CONFIG.whichtest == "LockMin") {
        run<sosilminim_t<2, 7> >();
    }
    else if (CONFIG.whichtest == "LockCache") {
        run<sosilcache_t<2, 7> >();
    }
    else if (CONFIG.whichtest == "Q64" || CONFIG.whichtest == "Q64W2D7") {
        run<Mindicator<2, 7, qc32_node_t> >();
    }
    else if (CONFIG.whichtest == "Q64W2D5") {
        run<Mindicator<2, 5, qc32_node_t> >();
    }
    else if (CONFIG.whichtest == "Q64W4D4") {
        run<Mindicator<4, 4, qc32_node_t> >();
    }
    else if (CONFIG.whichtest == "Q64W8D3") {
        run<Mindicator<8, 3, qc32_node_t> >();
    }
    else if (CONFIG.whichtest == "Q64W4D3") {
        run<Mindicator<4, 3, qc32_node_t> >();
    }
    else if (CONFIG.whichtest == "L64" || CONFIG.whichtest == "L64W2D7") {
        run<Mindicator<2, 7, lin32_node_t> >();
    }
    else if (CONFIG.whichtest == "L64W2D5") {
        run<Mindicator<2, 5, lin32_node_t> >();
    }
    else if (CONFIG.whichtest == "L64W4D4") {
        run<Mindicator<4, 4, lin32_node_t> >();
    }
    else if (CONFIG.whichtest == "L64W8D3") {
        run<Mindicator<8, 3, lin32_node_t> >();
    }
    else if (CONFIG.whichtest == "L64W4D3") {
        run<Mindicator<4, 3, lin32_node_t> >();
    }
    else if (CONFIG.whichtest == "W64") {
        run<sosiwminim64_t<2, 7> >();
    }
    else if (CONFIG.whichtest == "XQ64") {
        run<xsosiq64_t<2, 7> >();
    }
    else if (CONFIG.whichtest == "XL64") {
        run<xsosir64_t<2, 7> >();
    }
    else if (CONFIG.whichtest == "fArray") {
        run<Mindicator<2, 7, farray_node_t> >();
    }
    else if (CONFIG.whichtest == "RTM") {
        run<Mindicator<2, 7, RTM_node_t> >();
    }
    else if (CONFIG.whichtest == "RTM_fgl") {
        run<sosilRTM_fgl_t<2, 7> >();
    }
    else if (CONFIG.whichtest == "RTM_cgl") {
        run<sosilRTM_cgl_t<2, 7> >();
    }
    else if (CONFIG.whichtest == "cgl") {
        run<sosil_cgl_t<2, 7> >();
    }
    else {
        usage();
    }
    return 0;
}

