//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2011
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
#include <cstdlib>
#include <ctime>
#include <set>
#include <pthread.h>

#include "../common/locks.hpp"
#include "../alt-license/rand_r_32.h"

#include "mound_seq.hpp"
#include "mound_fc.hpp"
#include "mound_fgl.hpp"
#include "mound_dcas.hpp"
#include "mound_lazylf.hpp"
#include "std_pqueue.hpp"
#include "heap_seq.hpp"
#include "heap_inline.hpp"
#include "heap_hunt.hpp"
#include "list_seq.hpp"
#include "skip_queue_qc.hpp"
#include "skip_queue_lin.hpp"

#include "mound_RTM_fgl.hpp"
#include "mound_RTM_cgl.hpp"
#include "mound_RTM_dcas_2.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;

uint32_t NUM_THREADS = 0;
volatile static bool mound_bench_stop_flag = false;

template<class MOUND>
struct mound_visitor_thread_args_t
{
    uint32_t   id;
    MOUND *    mound;
    uint32_t   insert_per_round;
    uint32_t   remove_per_round;
    uint32_t   num_visit;
    uint32_t   seed;
};

inline static uint32_t random(uint32_t * seed)
{
    uint32_t temp = rand_r_32(seed);
    while (temp == 0 || temp == UINT_MAX)
        temp = rand_r_32(seed);
    return temp;
}

template <class MOUND>
static void* mound_visitor_strict(void* arg)
{
    mound_visitor_thread_args_t<MOUND>* v =
        (mound_visitor_thread_args_t<MOUND>*)arg;
    MOUND & mound = *v->mound;

    while (!mound_bench_stop_flag) {
        // a bunch of insertion
        for (uint32_t i = 0; i < v->insert_per_round; i++)
            mound.add(random(&v->seed));
        // followed by removals
        for (uint32_t i = 0; i < v->remove_per_round; i++)
            mound.remove();
        // increment throughput counter
        v->num_visit++;
    }

    return 0;
}

template <class MOUND>
static void* mound_visitor_relaxed(void* arg)
{
    mound_visitor_thread_args_t<MOUND>* v =
        (mound_visitor_thread_args_t<MOUND>*)arg;
    MOUND & mound = *v->mound;

    while (!mound_bench_stop_flag) {
        
        if (rand_r(&v->seed) % 2 == 0)
            // a bunch of insertion
            for (uint32_t i = 0; i < v->insert_per_round; i++)
                mound.add(random(&v->seed));
        else
            // a bunch of removals
            for (uint32_t i = 0; i < v->remove_per_round; i++)
                mound.remove();
        // increment throughput counter
        v->num_visit++;
    }

    return 0;
}

template <class MOUND>
static void* mound_visitor_balanced(void* arg)
{
    mound_visitor_thread_args_t<MOUND>* v =
        (mound_visitor_thread_args_t<MOUND>*)arg;
    MOUND & mound = *v->mound;

    while (!mound_bench_stop_flag) {
        uint32_t num = mound.remove();
        if (num != UINT_MAX)
            mound.add(num);
        // increment throughput counter
        v->num_visit++;
    }
    return 0;
}

template <class MOUND>
static void* mound_visitor_increase(void* arg)
{
    mound_visitor_thread_args_t<MOUND>* v =
        (mound_visitor_thread_args_t<MOUND>*)arg;
    MOUND & mound = *v->mound;

    uint32_t num = 1;
    while (!mound_bench_stop_flag) {
        // a bunch of insertion
        for (uint32_t i = 0; i < v->insert_per_round; i++)
            mound.add(num++);
        // followed by removals
        for (uint32_t i = 0; i < v->remove_per_round; i++)
            mound.remove();
        // increment throughput counter
        v->num_visit++;
    }

    return 0;
}

template <class MOUND>
static void* mound_visitor_decrease(void* arg)
{
    mound_visitor_thread_args_t<MOUND>* v =
        (mound_visitor_thread_args_t<MOUND>*)arg;
    MOUND & mound = *v->mound;

    uint32_t num = UINT_MAX - 1;
    while (!mound_bench_stop_flag) {
        // a bunch of insertion
        for (uint32_t i = 0; i < v->insert_per_round; i++)
            mound.add(num--);
        // followed by removals
        for (uint32_t i = 0; i < v->remove_per_round; i++)
            mound.remove();
        // increment throughput counter
        v->num_visit++;
    }

    return 0;
}

template<class PQ>
void sanity_check_sequential()
{
    const int max = 10000;
    std_pqueue_t contrast;
    PQ m;
    uint32_t seed = 0;

    for (int i = 0; i < max; i++) {
        uint32_t temp = random(&seed);
        contrast.add(temp);
        m.add(temp);
    }

    for (int i = 0; i < max - 1; i++) {
        uint32_t r1 = m.remove();
        uint32_t r2 = contrast.remove();

        if (r1 != r2) {
            cout << "different element at index " << i << ":"
                 << r1 << " " << r2 <<  endl;
            return;
        }
    }
    cout << "   okay" << endl;
}

template<class MOUND>
void sanity_check_concurrent()
{
    const uint32_t NUM_THREADS = 8;
    const uint32_t INIT_SIZE = 65536;
    const uint32_t INSERT_PER_ROUND = 1;
    const uint32_t REMOVE_PER_ROUND = 1;
    const uint32_t SLEEP_TIME = 1;

    mound_visitor_thread_args_t<MOUND> args1[NUM_THREADS];
    pthread_t tid1[NUM_THREADS];

    MOUND s;
    uint32_t seed = 0;

    for (uint32_t j = 0; j < INIT_SIZE; j++)
        s.add(random(&seed));

    for (uint32_t j = 0; j < NUM_THREADS; j++) {
        args1[j].id = j;
        args1[j].mound = &s;
        args1[j].num_visit = 0;
        args1[j].insert_per_round = INSERT_PER_ROUND;
        args1[j].remove_per_round = REMOVE_PER_ROUND;
        args1[j].seed = j; // NB: each thread gets its own seed, but the same
                           // seeds are used every time.
    }

    srand(time(NULL));
    mound_bench_stop_flag = false;
    for (uint32_t j = 0; j < NUM_THREADS; j++)
        pthread_create(&tid1[j], NULL, &mound_visitor_strict<MOUND>, &args1[j]);
    sleep(SLEEP_TIME);
    mound_bench_stop_flag = true;
    for (uint32_t j = 0; j < NUM_THREADS; j++)
        pthread_join(tid1[j], NULL);

    uint32_t old = 0;
    for (uint32_t i = 0; i < INIT_SIZE; i++) {
        uint32_t num = s.remove();
        if (old > num) {
            std::cerr << "error: heap invariant violated: "
                      << "prev = " << old << " "
                      << "curr = " << num << endl;
            return;
        }
        if (num == UINT_MAX) {
            std::cerr << "error: missing element(not linearizable)" << endl;
            return;
        }
        old = num;
    }

    uint32_t num = s.remove();
    if (num != UINT_MAX) {
        std::cerr << "error: extra element(not linearizable)" << endl;
        return;
    }

    cout << "   okay" << endl;
}


template<class MOUND>
static void mound_bench()
{
    const uint32_t INIT_SIZE = 65536;
    const uint32_t INSERT_PER_ROUND = 1;
    const uint32_t REMOVE_PER_ROUND = 1;
    const uint32_t SLEEP_TIME = 1;

    mound_visitor_thread_args_t<MOUND> args1[NUM_THREADS];
    pthread_t tid1[NUM_THREADS];

    MOUND s;
    uint32_t seed = 0;

    for (uint32_t j = 0; j < INIT_SIZE; j++)
        s.add(random(&seed));

    for (uint32_t j = 0; j < NUM_THREADS; j++) {
        args1[j].id = j;
        args1[j].mound = &s;
        args1[j].num_visit = 0;
        args1[j].insert_per_round = INSERT_PER_ROUND;
        args1[j].remove_per_round = REMOVE_PER_ROUND;
        args1[j].seed = j; // NB: each thread gets its own seed, but the same
                           // seeds are used every time.
    }

    srand(time(NULL));
    mound_bench_stop_flag = false;

    for (uint32_t j = 0; j < NUM_THREADS; j++)
        pthread_create(&tid1[j], NULL, &mound_visitor_relaxed<MOUND>, &args1[j]);

    sleep(SLEEP_TIME);
    //usleep(300000);
    mound_bench_stop_flag = true;

    for (uint32_t j = 0; j < NUM_THREADS; j++)
        pthread_join(tid1[j], NULL);

    uint64_t total_visit = 0;
    for (uint32_t j = 0; j < NUM_THREADS; j++)
        total_visit += args1[j].num_visit;

    cout << NUM_THREADS << ", " << total_visit / SLEEP_TIME;
    cout << endl;
}

template<class MOUND>
void run(bool sanity, const char* pqname)
{
    const int arr[] = { 1, 2, 3, 4, 5, 6, 7, 8};
    if (sanity) {
        cout << "sanity check (sequential) " << pqname << "..  ";
        sanity_check_sequential<MOUND>();
        cout << "sanity check (concurrent) " << pqname << "..  ";
        sanity_check_concurrent<MOUND>();
    }
    else {
        for (unsigned int i = 0; i < sizeof(arr) / sizeof(int); ++i) {
            cout << pqname << ", ";
            NUM_THREADS = arr[i];
            mound_bench<MOUND>();
        }
    }
}

int main(int argc, char** argv)
{
    bool sanity = false;

    string T = "";

    // parse the command-line options
    int opt;
    while ((opt = getopt(argc, argv, "cT:")) != -1) {
        switch(opt) {
          case 'T': T = std::string(optarg); break;
          case 'c': sanity = true; break;
        }
    }

    if (T == "std")
        run<std_pqueue_t>(sanity, "std_pqueue_t");
    else if (T == "heap")
        run<heap_inline_t>(sanity, "heap_inline_t");
    else if (T == "hunt")
        run<heap_hunt_t>(sanity, "heap_hunt_t");
    else if (T == "list")
        run<list_seq_t>(sanity, "list_seq_t");
    else if (T == "seq")
        run<mound_seq_t>(sanity, "mound_seq_t");
    else if (T == "fc")
        run<mound_fc_t>(sanity, "mound_fc_t");
    else if (T == "fgl")
        run<mound_fgl_t>(sanity, "mound_fgl_t");
    else if (T == "dcas")
        run<mound_dcas_t>(sanity, "mound_dcas_t");
    else if (T == "skipqc")
        run<skip_queue_qc_t>(sanity, "skip_queue_qc_t");
    else if (T == "skiplin")
        run<skip_queue_lin_t>(sanity, "skip_queue_lin_t");
    else if (T == "lazylf")
        run<mound_lazylf_t>(sanity, "mound_lazylf_t");
    else if (T == "RTM_f")
        run<mound_RTM_fgl_t>(sanity, "mound_RTM_fgl_t");
    else if (T == "RTM_c")
        run<mound_RTM_cgl_t>(sanity, "mound_RTM_cgl_t");
    else if (T == "RTM_d")
        run<mound_RTM_dcas_t>(sanity, "mound_RTM_dcas_t");
    return 0;
}

