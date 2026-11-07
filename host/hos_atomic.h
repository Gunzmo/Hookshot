/* Compare/exchange returning the observed old value, as used by host counters and
   state machines. The runtime already requires clang/GCC atomic builtins. */
#ifndef HOS_ATOMIC_H
#define HOS_ATOMIC_H
#define HOS_ATOMIC_CAS(p, value, expected) __extension__ ({ \
    __typeof__(__atomic_load_n((p), __ATOMIC_RELAXED)) hos_cas_old = (expected); \
    __atomic_compare_exchange_n((p), &hos_cas_old, (value), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
    hos_cas_old; \
})
#endif
