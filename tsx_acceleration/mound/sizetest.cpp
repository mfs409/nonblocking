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

using std::cerr;
using std::cout;
using std::endl;
using std::string;

uint32_t OPS_PER_THREAD = 1048576;
uint32_t INIT_SIZE = 65536;
mound_seq_t mound;

inline static uint32_t random(uint32_t * seed)
{
    uint32_t temp = rand_r_32(seed);
    while (temp == 0 || temp == UINT_MAX)
        temp = rand_r_32(seed);
    return temp;
}

void randTest()
{
    printf("Inserting %d elements in RANDOM order\n", OPS_PER_THREAD);
    uint32_t seed = 1;
    for (uint32_t i = 0; i < OPS_PER_THREAD; ++i)
        mound.add(random(&seed));
    printf("Mound statistics after insertion\n");
    mound.analyze();
    printf("Removing %d elements\n", OPS_PER_THREAD/2);
    for (uint32_t i = 0; i < OPS_PER_THREAD/2; ++i)
        mound.remove();
    printf("Mound statistics after removal\n");
    mound.analyze();
}

void growTest()
{
    printf("Inserting %d elements in INCREASING order\n", OPS_PER_THREAD);
    for (uint32_t i = 0; i < OPS_PER_THREAD; ++i)
        mound.add(i);
    printf("Mound statistics after insertion\n");
    mound.analyze();
    printf("Removing %d elements\n", OPS_PER_THREAD/2);
    for (uint32_t i = 0; i < OPS_PER_THREAD/2; ++i)
        mound.remove();
    printf("Mound statistics after removal\n");
    mound.analyze();
}

void shrinkTest()
{
    printf("Inserting %d elements in DECREASING order\n", OPS_PER_THREAD);
    for (uint32_t i = 0; i < OPS_PER_THREAD; ++i)
        mound.add((2*OPS_PER_THREAD) - i);
    printf("Mound statistics after insertion\n");
    mound.analyze();
    printf("Removing %d elements\n", OPS_PER_THREAD/2);
    for (uint32_t i = 0; i < OPS_PER_THREAD/2; ++i)
        mound.remove();
    printf("Mound statistics after removal\n");
    mound.analyze();
}

int main(int argc, char** argv)
{
    string T = "";

    // parse the command-line options
    int opt;
    while ((opt = getopt(argc, argv, "N:T:O:I:")) != -1) {
        switch(opt) {
          case 'T': T = std::string(optarg); break;
          case 'O': OPS_PER_THREAD = atoi(optarg); break;
          case 'I': INIT_SIZE = atoi(optarg);break;
        }
    }

    cout << "CFG, T=" << T << ", O=" << OPS_PER_THREAD << ", I=" << INIT_SIZE << endl;

    if (T == "rand")
        randTest();
    else if (T == "shrink")
        shrinkTest();
    else if (T == "grow")
        growTest();
    return 0;
}
