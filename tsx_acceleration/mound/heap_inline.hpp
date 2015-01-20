///////////////////////////////////////////////////////////////////////////////
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

/** Priority queue implementation using binary heap(with data inlined). */

#include "../common/platform.hpp"

class heap_inline_t
{
  public:
    heap_inline_t()
    {
        lock = 0;
        arr = new uint32_t[HEAP_MAX_SIZE];
        // our root element is stored at index 1(not zero!)
        counter = 1;
    }

    ~heap_inline_t()
    {
        delete [] arr;
    }


    void add(uint32_t n)
    {
        tatas_acquire(&lock);

        // set n as the right most element
        arr[counter] = n;

        uint32_t c = counter;

        while (c != 1) { // while c is not root
            // parent of c
            uint32_t p = c / 2;

            if (arr[c] >= arr[p])
                break;

            // swap c up the tree
            uint32_t temp = arr[c];
            arr[c] = arr[p];
            arr[p] = temp;

            c = p;
        }

        // increment counter
        counter++;

        tatas_release(&lock);
    }

    uint32_t remove()
    {
        tatas_acquire(&lock);

        // heap is empty
        if (counter == 1) {
            tatas_release(&lock);
            return UINT_MAX;
        }

        // remove top element
        uint32_t result = arr[1];

        // move rightmost element to top
        arr[1] = arr[counter - 1];

        // decrement counter
        counter--;

        uint32_t p = 1;

        while (true) {
            uint32_t l = p * 2;
            uint32_t r = p * 2 + 1;

            // p is leaf
            if (l >= counter)
                break;

            // r is counter
            if (r >= counter) {
                // compare p and l
                if (arr[p] > arr[l]) {
                    // swap p and l
                    uint32_t temp = arr[p];
                    arr[p] = arr[l];
                    arr[l] = temp;
                }
                break;
            }

            // which child is smaller?
            uint32_t w = arr[l] < arr[r] ? l : r;
            if (arr[p] <= arr[w])
                break;

            // swap p and w
            uint32_t temp = arr[p];
            arr[p] = arr[w];
            arr[w] = temp;

            p = w;
        }

        tatas_release(&lock);
        return result;
    }

    void print(uint32_t n)
    {
        for (uint32_t i = 1; i < counter; i++)
            std::cerr << arr[i] << std::endl;
    }


  private:
    volatile uintptr_t lock;
    uint32_t * arr;
    uint32_t counter;
};

