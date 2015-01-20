///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007, 2008, 2009
// University of Rochester
// Department of Computer Science
// All rights reserved.
//
// Copyright (c) 2009, 2010
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

#ifndef MINDICATOR_COMMON_HPP__
#define MINDICATOR_COMMON_HPP__

#include <limits.h>

namespace mindicator
{
/*** USEFUL TEMPLATES FOR COMPILE-TIME COMPUTATION OF CONSTANTS */

/*** Compute the Num to the power of Exp. */
template <int Num, int Exp>
struct Power
{
    enum { value = Power<Num, (Exp - 1)>::value * Num };
};
template <int Num>
struct Power<Num, 0>
{
    enum { value = 1 };
};

/**
 * Compute the sum of geometric series.
 *  First: the first term of series.
 *  Scalar: scalar of the series
 *  Term: number of terms
 */
template<int First, int Scalar, int Terms>
struct GeoSum
{
    enum {
        value = First * (Power<Scalar, Terms>::value - 1) / (Scalar - 1)
    };
};
template<int First, int Scalar>
struct GeoSum<First, Scalar, 1>
{
    enum { value = First };
};

/**
 *  A 64-bit packed struct holding a value, a 31-bit counter, and a
 *  bool.  Read with mvx if you need atomicity.
 */
// union word_t {
//     // [mfs] If we made this a struct of a struct, so that state/ver
//     //       was one field and min was its own, clean field, then we
//     //       could avoid endian issues while doing 32-bit atomic ops on
//     //       the min field.
//     volatile struct {
//         uint32_t state : 1;   // state {tentative, steady}
//         uint32_t ver   : 31;  // version number
//         int32_t  min   : 32;  // cache of min value of children
//     } fields;
//     volatile uint64_t all;
// } __attribute__ ((aligned(8)));

union word64_t
{
    volatile struct
    {
        union
        {
            struct
            {
                uint32_t steady : 1;  // 1 = STEADY, 0 = TENTATIVE
                uint32_t ver    : 31; // version number
            } bits;
            uint32_t sv;              // for reading 32 bits at once
        } word;
        int32_t  min    : 32;         // cache of min value of children
    } fields;
    volatile uint64_t all;            // for reading 64 bits at once
} __attribute__ ((aligned(8)));

/*** Track the different states that an intermediate node can be in */
enum node_state_t {
    TENTATIVE = 0,  // this node has an arrive() operation that has not come
    // back down the tree yet
    STEADY    = 1   // no outstanding operations on this node?
};

/** Max number supported. */
static const int32_t TOP = INT_MAX;

/*** HELPER MACROS */
#define MAKE_WORD(_var, _s, _d, _v)                                     \
    { _var.fields.word.bits.steady = _s; _var.fields.min = _d; _var.fields.word.bits.ver = _v; }

inline void read_word(const volatile word64_t * src, volatile word64_t * dest)
{
#if 1
    mvx(&src->all, &dest->all);
#else
    uint32_t v1, v2;
    do {
        v1 = src->fields.word.sv;
        CFENCE;
        dest->all = src->all;
        CFENCE;
        v2 = src->fields.word.sv;
    } while (v1 != v2);
#endif
}

/**
 *  Quick and dirty inline function for setting the three parts of a word_t
 */
inline void reinit_word(word64_t& w, uint32_t s, uint32_t d, uint32_t v)
{
    w.fields.word.bits.steady = s;
    w.fields.min = d;
    w.fields.word.bits.ver = v;
}

} // namespace mindicator

#endif // MINDICATOR_COMMON_HPP__
