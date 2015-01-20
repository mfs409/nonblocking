//////////////////////////////////////////////////////////////////////////////
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

#include "fraser.hpp"

class skip_queue_qc_t
{
  private:
    sl_intset_t * slset;

  public:
    skip_queue_qc_t()
    {
        slset = sl_set_new();
    }

    void add(uint32_t n)
    {
        fraser_insert(slset, n, false);
    }

    uint32_t remove()
    {
        uint32_t min = findAndMarkMin();
        if (min == UINT_MAX)
            return UINT_MAX;
        fraser_remove(slset, min);
        return min;
    }

    uint32_t findAndMarkMin()
    {
        sl_node_t * curr = slset->head->nexts[0];
        while (curr != slset->tail) {
            if (curr->deleted == 0) {
                if (bcas32(&curr->deleted, 0, 1)) {
                    return curr->val;
                }
            }
            sl_node_t * right = (sl_node_t*)unset_mark((uintptr_t)curr->nexts[0]);
            curr = right;
        }
        return UINT_MAX;
    }
};


