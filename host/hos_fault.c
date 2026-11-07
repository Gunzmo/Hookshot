#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include "hos_fault.h"
#include <pthread.h>
#include <signal.h>
#include <sys/uio.h>
#include <unistd.h>
#include <ucontext.h>
#include <string.h>
#include <stdlib.h>
static HosFaultHandler handlers[16], reporter;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t registration = PTHREAD_MUTEX_INITIALIZER;
static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP};
static struct sigaction previous[5];
static void fault(int sig, siginfo_t *info, void *context) {
    ucontext_t *u = context;
    HosFault f = {sig, (sig == SIGSEGV || sig == SIGBUS) && (u->uc_mcontext.gregs[REG_ERR] & 2),
                  (uintptr_t)info->si_addr, (uintptr_t)u->uc_mcontext.gregs[REG_RIP], context};
    for (int i = 0; i < 16; i++) {
        HosFaultHandler fn = __atomic_load_n(&handlers[i], __ATOMIC_ACQUIRE);
        if (fn && fn(&f)) return;
    }
    HosFaultHandler fn = __atomic_load_n(&reporter, __ATOMIC_ACQUIRE);
    if (fn && fn(&f)) return;
    for (int i = 0; i < 5; i++) if (signals[i] == sig) {
        struct sigaction *old = &previous[i];
        if (old->sa_handler == SIG_IGN) return;
        if (old->sa_handler != SIG_DFL) {
            if (old->sa_flags & SA_SIGINFO) old->sa_sigaction(sig, info, context);
            else old->sa_handler(sig);
            return;
        }
        sigaction(sig, old, NULL);
        raise(sig);
        return;
    }
}
static void install(void) {
    struct sigaction sa = {0};
    sa.sa_sigaction = fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < 5; i++) sigaction(signals[i], &sa, &previous[i]);
}
int hos_fault_add(HosFaultHandler fn) {
    pthread_once(&once, install);
    pthread_mutex_lock(&registration);
    int ok = 0;
    for (int i = 0; i < 16; i++) if (!handlers[i]) {
        __atomic_store_n(&handlers[i], fn, __ATOMIC_RELEASE); ok = 1; break;
    }
    pthread_mutex_unlock(&registration);
    return ok;
}
void hos_fault_reporter(HosFaultHandler fn) {
    pthread_once(&once, install);
    __atomic_store_n(&reporter, fn, __ATOMIC_RELEASE);
}
int hos_guarded_copy(void *dst, const void *src, size_t size) {
    if (!size) return 1;
    struct iovec local = {dst, size}, remote = {(void *)src, size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)size;
}
#endif
