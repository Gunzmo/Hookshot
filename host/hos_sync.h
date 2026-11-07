/* Host synchronization. These objects live on the host, never in guest ABI structs.
   Monitors use mutexes (a condition wait must release and reacquire that same mutex);
   tables with shared readers use rwlocks. */
#ifndef HOS_SYNC_H
#define HOS_SYNC_H
#include <stdint.h>

#define HOS_WAIT_FOREVER UINT32_MAX

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <errno.h>
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t HosMutex;
typedef pthread_rwlock_t HosRwLock;
typedef pthread_cond_t HosCond;
typedef pthread_mutex_t HosRecursiveMutex;
#define HOS_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define HOS_RWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
#define HOS_COND_INIT PTHREAD_COND_INITIALIZER
#define HOS_RECURSIVE_MUTEX_INIT PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
static inline void hos_mutex_init(HosMutex *m) { pthread_mutex_init(m, NULL); }
static inline void hos_mutex_destroy(HosMutex *m) { pthread_mutex_destroy(m); }
static inline void hos_mutex_lock(HosMutex *m) { pthread_mutex_lock(m); }
static inline void hos_mutex_unlock(HosMutex *m) { pthread_mutex_unlock(m); }
static inline void hos_rwlock_read(HosRwLock *m) { pthread_rwlock_rdlock(m); }
static inline void hos_rwlock_write(HosRwLock *m) { pthread_rwlock_wrlock(m); }
static inline void hos_rwlock_write_unlock(HosRwLock *m) { pthread_rwlock_unlock(m); }
static inline void hos_cond_init(HosCond *c) { pthread_cond_init(c, NULL); }
static inline void hos_cond_signal(HosCond *c) { pthread_cond_signal(c); }
static inline void hos_cond_broadcast(HosCond *c) { pthread_cond_broadcast(c); }
static inline int hos_cond_wait(HosCond *c, HosMutex *m, uint32_t ms) {
    if (ms == HOS_WAIT_FOREVER) return pthread_cond_wait(c, m) == 0;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    end.tv_sec += ms / 1000;
    end.tv_nsec += (long)(ms % 1000) * 1000000;
    if (end.tv_nsec >= 1000000000) end.tv_sec++, end.tv_nsec -= 1000000000;
    return pthread_cond_clockwait(c, m, CLOCK_MONOTONIC, &end) == 0;
}
static inline void hos_recursive_lock(HosRecursiveMutex *m) { pthread_mutex_lock(m); }
static inline void hos_recursive_unlock(HosRecursiveMutex *m) { pthread_mutex_unlock(m); }
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef SRWLOCK HosMutex;
typedef SRWLOCK HosRwLock;
typedef CONDITION_VARIABLE HosCond;
typedef struct { volatile int ready; CRITICAL_SECTION native; } HosRecursiveMutex;
#define HOS_MUTEX_INIT SRWLOCK_INIT
#define HOS_RWLOCK_INIT SRWLOCK_INIT
#define HOS_COND_INIT CONDITION_VARIABLE_INIT
#define HOS_RECURSIVE_MUTEX_INIT {0}
static inline void hos_mutex_init(HosMutex *m) { InitializeSRWLock(m); }
static inline void hos_mutex_destroy(HosMutex *m) { (void)m; }
static inline void hos_mutex_lock(HosMutex *m) { AcquireSRWLockExclusive(m); }
static inline void hos_mutex_unlock(HosMutex *m) { ReleaseSRWLockExclusive(m); }
static inline void hos_rwlock_read(HosRwLock *m) { AcquireSRWLockShared(m); }
static inline void hos_rwlock_write(HosRwLock *m) { AcquireSRWLockExclusive(m); }
static inline void hos_rwlock_write_unlock(HosRwLock *m) { ReleaseSRWLockExclusive(m); }
/* Shared and exclusive SRW unlocks differ on Windows. */
static inline void hos_rwlock_read_unlock(HosRwLock *m) { ReleaseSRWLockShared(m); }
static inline void hos_cond_init(HosCond *c) { InitializeConditionVariable(c); }
static inline void hos_cond_signal(HosCond *c) { WakeConditionVariable(c); }
static inline void hos_cond_broadcast(HosCond *c) { WakeAllConditionVariable(c); }
static inline int hos_cond_wait(HosCond *c, HosMutex *m, uint32_t ms) {
    return SleepConditionVariableSRW(c, m, ms, 0);
}
static inline void hos_recursive_lock(HosRecursiveMutex *m) {
    if (__atomic_load_n(&m->ready, __ATOMIC_ACQUIRE) != 2) {
        int zero = 0;
        if (__atomic_compare_exchange_n(&m->ready, &zero, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            InitializeCriticalSection(&m->native);
            __atomic_store_n(&m->ready, 2, __ATOMIC_RELEASE);
        } else while (__atomic_load_n(&m->ready, __ATOMIC_ACQUIRE) != 2) SwitchToThread();
    }
    EnterCriticalSection(&m->native);
}
static inline void hos_recursive_unlock(HosRecursiveMutex *m) { LeaveCriticalSection(&m->native); }
#endif
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
static inline void hos_rwlock_read_unlock(HosRwLock *m) { pthread_rwlock_unlock(m); }
#endif
#endif
