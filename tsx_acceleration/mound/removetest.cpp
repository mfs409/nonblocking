//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2014
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
#include "mound_fgl.hpp"
#include "mound_dcas.hpp"
#include "mound_lazylf.hpp"
#include "std_pqueue.hpp"
#include "heap_seq.hpp"
#include "heap_inline.hpp"
#include "heap_hunt.hpp"
#include "list_seq.hpp"
#include "skip_queue_lin.hpp"
#include "skip_queue_qc.hpp"
#include "mound_RTM_fgl.hpp"
#include "mound_RTM_cgl.hpp"
#include "mound_RTM_dcas_2.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;

uint32_t NUM_THREADS = 1;
uint32_t OPS_PER_THREAD = 1048576;
uint32_t INIT_SIZE = 0;
volatile bool START_TEST = false;

volatile int garbage[1048576];
volatile uint32_t READY = 0;

void clearcache(int id)
{
    for (int i = 0; i < 1048576; ++i)
        garbage[i] += id;
    fai32(&READY);
}


template<class MOUND>
struct mound_arg_t
{
    uint32_t   id;
    MOUND *    mound;
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
static void* remove_test(void* arg)
{
    mound_arg_t<MOUND>* v = (mound_arg_t<MOUND>*)arg;
    MOUND & mound = *v->mound;

    if (v->id != 0)
        clearcache(v->id);

    while (!START_TEST) { }

    for (uint32_t i = 0; i < OPS_PER_THREAD; ++i) {
        mound.remove();
    }

    return 0;
}

template<class MOUND>
static void mound_bench(const char* pqname)
{
    mound_arg_t<MOUND> args1[NUM_THREADS];
    pthread_t tid1[NUM_THREADS];

    MOUND s;
    uint32_t seed = 0;

    std::cout << "Initializing ";
    for (uint32_t j = 0; j < INIT_SIZE; j++) {

        if ((j%(1<<20))==0) { cout << "."; cout.flush(); }
        s.add(random(&seed));
    }
    std::cout << "done" << std::endl;

    for (uint32_t j = 0; j < NUM_THREADS; j++) {
        args1[j].id = j;
        args1[j].mound = &s;
        args1[j].seed = j; // NB: each thread gets its own seed, but the same
                           // seeds are used every time.
    }

    for (uint32_t j = 1; j < NUM_THREADS; j++)
        pthread_create(&tid1[j], NULL, &remove_test<MOUND>, &args1[j]);

    clearcache(0);
    while (READY != NUM_THREADS) { }

    uint64_t start = getElapsedTime();
    START_TEST = true;
    remove_test<MOUND>(&args1[0]);

    for (uint32_t j = 1; j < NUM_THREADS; j++)
        pthread_join(tid1[j], NULL);

    uint64_t stop = getElapsedTime();

    double ops = NUM_THREADS * OPS_PER_THREAD;
    double dThroughput = 1000000000 * ops / (stop - start);
    unsigned long long throughput = (unsigned long long)dThroughput;
    cout << "duration = " << (stop-start) << endl;
    cout << pqname << ", " << NUM_THREADS << ", " << throughput << endl;
}

int main(int argc, char** argv)
{
    string T = "";

    // parse the command-line options
    int opt;
    while ((opt = getopt(argc, argv, "N:T:O:I:")) != -1) {
        switch(opt) {
          case 'T': T = std::string(optarg); break;
          case 'N': NUM_THREADS = atoi(optarg); break;
          case 'O': OPS_PER_THREAD = atoi(optarg); break;
          case 'I': INIT_SIZE = atoi(optarg);break;
        }
    }
    if (INIT_SIZE == 0)
        INIT_SIZE = NUM_THREADS * OPS_PER_THREAD * 2;

    cout << "CFG, T=" << T << ", N=" << NUM_THREADS << ", O=" << OPS_PER_THREAD << ", I=" << INIT_SIZE << endl;

    if (T == "std")
        mound_bench<std_pqueue_t>("std_pqueue_t");
    else if (T == "heap")
        mound_bench<heap_inline_t>("heap_inline_t");
    else if (T == "hunt")
        mound_bench<heap_hunt_t>("heap_hunt_t");
    else if (T == "list")
        mound_bench<list_seq_t>("list_seq_t");
    else if (T == "seq")
        mound_bench<mound_seq_t>("mound_seq_t");
    else if (T == "fgl")
        mound_bench<mound_fgl_t>("mound_fgl_t");
    else if (T == "dcas")
        mound_bench<mound_dcas_t>("mound_dcas_t");
    else if (T == "skipqc")
        mound_bench<skip_queue_qc_t>("skip_queue_qc_t");
    else if (T == "skiplin")
        mound_bench<skip_queue_lin_t>("skip_queue_lin_t");
    else if (T == "lazylf")
        mound_bench<mound_lazylf_t>("mound_lazylf_t");
    else if (T == "RTM_f")
        mound_bench<mound_RTM_fgl_t>("mound_RTM_fgl_t");
    else if (T == "RTM_c")
        mound_bench<mound_RTM_cgl_t>("mound_RTM_cgl_t");
    else if (T == "RTM_d")
        mound_bench<mound_RTM_dcas_t>("mound_RTM_dcas_t");
    return 0;
}

