#pragma once

#include <atomic>

#define bcas(p, o, n)   std::atomic_compare_exchange_strong((p), (o), (n))
#define faiu(c)         std::atomic_fetch_add((c), (uint32_t)1)
#define xld(a)          std::atomic_load_explicit((a), memory_order::memory_order_relaxed)
#define xst(a, v)       std::atomic_store_explicit((a), (v), memory_order::memory_order_relaxed)

#define IS_MARKED(x)     ((uintptr_t)(x) & 0x1)
#define REF_MARKED(x)    ((uintptr_t)(x) | 0x1)
#define REF_UNMARKED(x)  ((uintptr_t)(x) & ~0x1)

static const uintptr_t MAX_THREADS = 64;
static const uintptr_t CACHELINE_BYTES = 64;

struct pad_word_t {
    std::atomic<uintptr_t> val;
    char pad[CACHELINE_BYTES-sizeof(uintptr_t)];
};

template<typename T>
union alignas(8) cptr_t
{
    struct
    {
        T *      ptr;
        uint32_t ctr;
    } fields;
    uint64_t all;
};

#define MAKE_CPTR(w, p, c) { (w).fields.ptr = p; (w).fields.ctr = (c); }

#define nop()               asm volatile("nop")

/** Issue 64 nops to provide a little busy waiting */
inline void spin64()
{
    for (int i = 0; i < 64; i++)
        nop();
}
