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
#include "common.hpp"

class list_seq_t
{
  public:
    list_seq_t()
    {
        lock = 0;
        head = NULL;
    }

    ~list_seq_t()
    {
    }

    void add(uint32_t n)
    {
        tatas_acquire(&lock);

        mound_list_t * curr = head;
        mound_list_t * prev = NULL;

        while (curr) {
            if (n <= curr->data)
                break;
            prev = curr;
            curr = curr->next;
        }

        mound_list_t * node = mound_list_pool::get();
        node->data = n;
        node->next = curr;

        if (prev)
            prev->next = node;
        else
            head = node;

        tatas_release(&lock);
    }

    uint32_t remove()
    {
        tatas_acquire(&lock);
        uint32_t result;
        if (!head)
            result = UINT_MAX;
        else {
            mound_list_t * temp = head;
            result = head->data;
            head = head->next;
            mound_list_pool::put(temp);
        }
        tatas_release(&lock);
        return result;
    }

  private:
    volatile uintptr_t      lock;
    mound_list_t * volatile head;
};

