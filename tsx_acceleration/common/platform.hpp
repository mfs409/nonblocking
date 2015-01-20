///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2005, 2006, 2007, 2008, 2009
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

#ifndef PLATFORM_HPP__
#define PLATFORM_HPP__

/**
 *  This file hides differences that are based on compiler, CPU, and OS
 */

#define __STDC_LIMIT_MACROS // for INT32_MAX

#include "../config.h"
#include <stdint.h>
#include <limits.h>

/**
 *  We are going to hard-code the cacheline size at 64 bytes.  This
 *  should be OK for SPARC/X86, though occasionally inefficient
 */
static const uint32_t CACHELINE_BYTES = 64; // words, not bytes

/**
 *  The first task for this file is to declare atomic operations (cas, swap,
 *  etc) and custom assembly codes, such as compiler fences, memory barriers,
 *  and no-op instructions.  This code depends on the compiler and processor.
 */


/**
 *  icc is nominally an x86/x86-64 compiler that supports sync builtins,
 *  however the stm prototype doesn't support operations on pointer types,
 *  which we perform all the time. This header performs the fixes.
 */
#if defined(STM_CPU_X86) && defined(__ICC)
#   include "icc-sync.hpp"
#endif


/**
 *  Here is the declaration of atomic operations when we're on an x86 (32bit or
 *  64bit) and using the GNU compiler collection.  This assumes that the
 *  compiler is recent enough that it supports the builtin __sync operations
 */
#if defined(STM_CPU_X86) && !defined(STM_CC_SUN)

#define CFENCE              asm volatile ("":::"memory")
#define WBR                 __sync_synchronize()

#define cas32(p, o, n)      __sync_val_compare_and_swap(p, o, n)
#define cas64(p, o, n)      __sync_val_compare_and_swap(p, o, n)
#define casptr(p, o, n)     __sync_val_compare_and_swap(p, o, n)
#define bcas32(p, o, n)     __sync_bool_compare_and_swap(p, o, n)
#define bcas64(p, o, n)     __sync_bool_compare_and_swap(p, o, n)
#define bcasptr(p, o, n)    __sync_bool_compare_and_swap(p, o, n)

#define tas(p)              __sync_lock_test_and_set(p, 1)

#define nop()               asm volatile("nop")

// NB: GCC implements test_and_set via swap
#define atomicswap8(p, v)   __sync_lock_test_and_set(p, v)
#define atomicswap32(p, v)  __sync_lock_test_and_set(p, v)
#define atomicswap64(p, v)  __sync_lock_test_and_set(p, v)
#define atomicswapptr(p, v) __sync_lock_test_and_set(p, v)

#define fai32(p)            __sync_fetch_and_add(p, 1)
#define fai64(p)            __sync_fetch_and_add(p, 1)
#define faiptr(p)           __sync_fetch_and_add(p, 1)
#define faa32(p, a)         __sync_fetch_and_add(p, a)
#define faa64(p, a)         __sync_fetch_and_add(p, a)
#define faaptr(p, a)        __sync_fetch_and_add(p, a)

/**
 *  cpuid function, for flushing the pipeline when doing self/cross-modifying
 *  code
 */
inline void cpuid()
{
    int a;
    asm volatile("cpuid;"
                 : "=a"(a)
                 : "a"(0)
                 : "%ebx", "%ecx", "%edx");
}

#endif

/**
 *  Here is the declaration of atomic operations when we're on a sparc (32bit)
 *  and using the GNU compiler collection.  For some reason, gcc 4.3.1 __sync_*
 *  operations can sometimes cause odd compiler crashes, so we provide our own
 *  assembly and use it instead.
 *
 *  NB: gcc doesn't provide a builtin equivalent to the SPARC swap instruction,
 *      and thus we have to implement atomicswap ourselves.
 */
#if defined(STM_CPU_SPARC) && defined (STM_CC_GCC)
#define CFENCE          asm volatile ("":::"memory")
#define WBR             __sync_synchronize()

/**
 * 32-bit CAS via SPARC CAS instruction
 */
inline uint32_t internal_cas32(volatile uint32_t* ptr, uint32_t old,
                               uint32_t _new)
{
    asm volatile("cas [%2], %3, %0"                     // instruction
                 : "=&r"(_new)                          // output
                 : "0"(_new), "r"(ptr), "r"(old)        // inputs
                 : "memory");                           // side effects
    return _new;
}

/**
 *  64-bit CAS via SPARC CASX instruction.
 *
 *  NB: This code only works correctly with -m64 specified, as otherwise GCC
 *      refuses to use a 64-bit register to pass a value.
 */
inline uint64_t internal_cas64(volatile uint64_t* ptr, uint64_t old,
                               uint64_t _new)
{
    asm volatile("casx [%2], %3, %0"                    // instruction
                 : "=&r"(_new)                          // output
                 : "0"(_new), "r"(ptr), "r"(old)        // inputs
                 : "memory");                           // side effects
    return _new;
}

// we can't mov 64 bits directly from c++ to a register, so we must ldx
// pointers to get the data into registers
static inline bool bcas64_override(volatile unsigned long long* ptr,
                                   const unsigned long long* expected_value,
                                   const unsigned long long* new_value)
{
    bool success = false;

    asm volatile("ldx   [%1], %%o4;"
                 "ldx   [%2], %%o5;"
                 "casx  [%3], %%o4, %%o5;"
                 "cmp   %%o4, %%o5;"
                 "mov   %%g0, %0;"
                 "move  %%xcc, 1, %0"   // predicated move... should do this
                                        // for bool_cas too
                 : "=r"(success)
                 : "r"(expected_value), "r"(new_value), "r"(ptr)
                 : "o4", "o5", "memory");
    return success;
}


#define cas32(p, o, n)      internal_cas32((uint32_t*)(p), (uint32_t)(o), (uint32_t)(n))
#ifdef STM_BITS_64
#define cas64(p, o, n)      internal_cas64((uint64_t*)(p), (uint64_t)(o), (uint64_t)(n))
#define casptr(p, o, n)     cas64(p, o, n)
#else
#define cas64(p, o, n)      __sync_val_compare_and_swap(p, o, n)
#define casptr(p, o, n)     cas32(p, o, n)
#endif

#define bcas32(p, o, n)  ({ o == cas32(p, (o), (n)); })
#define bcas64(p, o, n)  ({ o == cas64(p, (o), (n)); })
#define bcasptr(p, o, n) ({ ((void*)o) == (void*)casptr(p, (o), (n)); })

#define tas(p)            __sync_lock_test_and_set(p, 1)

#define nop()             asm volatile("nop")

// NB: SPARC swap instruction only is 32/64-bit... there is no atomicswap8
#ifdef STM_BITS_32
#define atomicswapptr(p, v)                                 \
    ({                                                      \
        __typeof((v)) v1 = v;                               \
        __typeof((p)) p1 = p;                               \
        asm volatile("swap [%2], %0;"                       \
                     :"=r"(v1) :"0"(v1), "r"(p1):"memory"); \
        v1;                                                 \
    })
#else
#define atomicswapptr(p, v)                     \
    ({                                          \
        __typeof((v)) tmp;                      \
        while (1) {                             \
            tmp = *(p);                         \
            if (bcasptr((p), tmp, (v))) break;  \
        }                                       \
        tmp;                                    \
    })
#endif

#define faa32(p,a)                                              \
    ({ __typeof(*p) _f, _e;                                     \
       do { _e = _f; }                                          \
       while ((_f = (__typeof(*p))cas32(p, _e, (_e+a))) != _e); \
       _f;                                                      \
    })
#define fai32(p)            faa32(p,1)
#define faiptr(p)           __sync_fetch_and_add(p, 1)
#define faa64(p, a)         __sync_fetch_and_add(p, a)
#define faaptr(p, a)        __sync_fetch_and_add(p, a)
#endif

/**
 *  Here is the declaration of atomic operations when we're using Sun Studio
 *  12.1.  These work for x86 and SPARC, at 32-bit or 64-bit
 */
#if (defined(STM_CPU_X86) || defined(STM_CPU_SPARC)) && defined(STM_CC_SUN)
#include <atomic.h>
#define CFENCE              asm volatile("":::"memory")
#define WBR                 membar_enter()

#define cas32(p, o, n)      atomic_cas_32(p, (o), (n))
#define cas64(p, o, n)      atomic_cas_64(p, (o), (n))
#define casptr(p, o, n)     atomic_cas_ptr(p, (void*)(o), (void*)(n))
#define bcas32(p, o, n)     ({ o == cas32(p, (o), (n)); })
#define bcas64(p, o, n)     ({ o == cas64(p, (o), (n)); })
#define bcasptr(p, o, n)    ({ ((void*)o) == casptr(p, (o), (n)); })

#define tas(p)              atomic_set_long_excl((volatile unsigned long*)p, 0)

#define nop()               asm volatile("nop")

#define atomicswap8(p, v)   atomic_swap_8(p, v)
#define atomicswap32(p, v)  atomic_swap_32(p, v)
#define atomicswap64(p, v)  atomic_swap_64(p, v)
#define atomicswapptr(p, v) atomic_swap_ptr(p, (void*)(v))

#define fai32(p)            (atomic_inc_32_nv(p)-1)
#define fai64(p)            __sync_fetch_and_add(p, 1)
#define faiptr(p)           (atomic_inc_ulong_nv((volatile unsigned long*)p)-1)
#define faa32(p, a)         atomic_add_32(p, a)
#define faa64(p, a)         atomic_add_64(p, a)
#define faaptr(p, a)        atomic_add_long((volatile unsigned long*)p, a)

//  NB: must shut off 'builtin_expect' support
#define __builtin_expect(a, b) a
#endif

/**
 *  Now we must deal with the ability to load/store 64-bit values safely.  In
 *  32-bit mode, this is potentially a problem, so we handle 64-bit atomic
 *  load/store via the mvx() function.  mvx() depends on the bit level and the
 *  CPU
 */

#if defined(STM_BITS_64)
/**
 *  64-bit code is easy... 64-bit accesses are atomic
 */
inline void mvx(const volatile uint64_t* src, volatile uint64_t* dest)
{
    *dest = *src;
}
#endif

#if defined(STM_BITS_32) && defined(STM_CPU_X86)
/**
 *  32-bit on x86... cast to double
 */
inline void mvx(const volatile uint64_t* src, volatile uint64_t* dest)
{
    const volatile double* srcd = (const volatile double*)src;
    volatile double* destd = (volatile double*)dest;
    *destd = *srcd;
}
#endif

#if defined(STM_BITS_32) && defined(STM_CPU_SPARC)
/**
 *  32-bit on SPARC... use ldx/stx
 */
inline void mvx(const volatile uint64_t* from, volatile uint64_t* to)
{
    asm volatile("ldx  [%0], %%o4;"
                 "stx  %%o4, [%1];"
                 :: "r"(from), "r"(to)
                 : "o4", "memory");
}
#endif

/**
 *  The next task for this file is to establish access to a high-resolution CPU
 *  timer.  The code depends on the CPU and bit level.  It is identical for
 *  32/64-bit x86.  For sparc, the code depends on if we are 32-bit or 64-bit.
 */
#if defined(STM_CPU_X86)
/**
 *  On x86, we use the rdtsc instruction
 */
inline uint64_t tick()
{
    uint32_t tmp[2];
    asm ("rdtsc" : "=a" (tmp[1]), "=d" (tmp[0]) : "c" (0x10) );
    return (((uint64_t)tmp[0]) << 32) | tmp[1];
}
#endif

#if defined(STM_CPU_SPARC) && defined(STM_BITS_64)
/**
 *  64-bit SPARC: read the tick register into a regular (64-bit) register
 *
 *  This code is based on http://blogs.sun.com/d/entry/reading_the_tick_counter and
 *  http://sourceware.org/binutils/docs-2.20/as/Sparc_002dRegs.html
 */
inline uint64_t tick()
{
    uint64_t val;
    asm volatile("rd %tick, %0" :"=r"(val) : :);
    return val;
}
#endif

#if defined(STM_CPU_SPARC) && defined(STM_BITS_32)
/**
 *  32-bit SPARC: read the tick register into two 32-bit registers, then
 *  manually combine the result
 *
 *  This code is based on
 *    http://blogs.sun.com/d/entry/reading_the_tick_counter
 *  and
 *    http://sourceware.org/binutils/docs-2.20/as/Sparc_002dRegs.html
 */
inline uint64_t tick()
{
    uint32_t lo = 0, hi = 0;
    asm volatile("rd %tick, %%o2;"
                 "srlx %%o2,32,%0;"
                 "sra %%o2,0,%1;"
                 : "=r"(hi), "=r"(lo) : : "%o2" );
    uint64_t ans = hi;
    ans = ans << 32;
    ans |= lo;
    return ans;
}
#endif

/**
 *  Next, we provide a platform-independent function for sleeping for a number
 *  of milliseconds.  This code depends on the OS.
 *
 *  NB: since we do not have Win32 support, this is now very easy... we just
 *      use the usleep instruction.
 */
#include <unistd.h>
inline void sleep_ms(uint32_t ms) { usleep(ms*1000); }


/**
 *  Now we present a clock that operates in nanoseconds, instead of in ticks,
 *  and a function for yielding the CPU.  This code also depends on the OS
 */
#if defined(STM_OS_LINUX)
#include <stdio.h>
#include <cstring>
#include <assert.h>
#include <pthread.h>
#include <time.h>

/**
 *  Yield the CPU
 */
inline void yield_cpu() { pthread_yield(); }

/**
 *  The Linux clock_gettime is reasonably fast, has good resolution, and is not
 *  affected by TurboBoost.  Using MONOTONIC_RAW also means that the timer is
 *  not subject to NTP adjustments, which is preferably since an adjustment in
 *  mid-experiment could produce some funky results.
 */
inline uint64_t getElapsedTime()
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);

    uint64_t tt = (((long long)t.tv_sec) * 1000000000L) + ((long long)t.tv_nsec);
    return tt;
}

#endif // STM_OS_LINUX

#if defined(STM_OS_SOLARIS)
#include <sys/time.h>

/**
 *  Yield the CPU
 */
inline void yield_cpu() { yield(); }

/**
 *  We'll just use gethrtime() as our nanosecond timer
 */
inline uint64_t getElapsedTime()
{
    return gethrtime();
}

#endif // STM_OS_SOLARIS

#if defined(STM_OS_MACOS)
#include <mach/mach_time.h>
#include <sched.h>

/**
 *  Yield the CPU
 */
inline void yield_cpu() { sched_yield(); }

/**
 *  We'll use the MACH timer as our nanosecond timer
 *
 *  This code is based on code at
 *  http://developer.apple.com/qa/qa2004/qa1398.html
 */
inline uint64_t getElapsedTime()
{
    static mach_timebase_info_data_t sTimebaseInfo;
    if (sTimebaseInfo.denom == 0)
        (void)mach_timebase_info(&sTimebaseInfo);
    return mach_absolute_time() * sTimebaseInfo.numer / sTimebaseInfo.denom;
}
#endif // STM_OS_MACOS

/**
 *  Some compilers on some platforms handle some attributes oddly.  Here we try
 *  to hide these issues
 */

/**
 *  Macro for regparm calling convention on x86
 */
#if defined(STM_CPU_X86) && (defined(STM_CC_GCC) || defined(STM_CC_LLVM))
#define TM_FASTCALL __attribute__((regparm(3)))
#else
#define TM_FASTCALL
#endif

/**
 *  For some reason, if we force too much to be inlined, then the whole
 *  compilation breaks on sparc/solaris/g++4.3.2.  This lets us turn off some
 *  forced inlining
 */
#if defined(STM_CPU_SPARC) && defined (STM_CC_GCC)
#define TM_INLINE
#else
#define TM_INLINE __attribute__((always_inline))
#endif

/**
 *  Hide compiler-specific alignment directives
 */
#if defined(STM_CC_GCC)
#define TM_ALIGN(x) __attribute__((aligned(x)))
#else
#define TM_ALIGN(x)
#endif

#endif // PLATFORM_HPP__
