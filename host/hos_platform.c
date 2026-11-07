#include "hos_platform.h"
#include <stdlib.h>
#include <stdio.h>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

size_t hos_executable_path(char *path, size_t capacity) {
    if (!capacity) return 0;
    path[0] = 0;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    const ssize_t n = readlink("/proc/self/exe", path, capacity - 1);
    if (n < 0 || (size_t)n >= capacity - 1) { path[0] = 0; return 0; }
    path[n] = 0;
#else
    const size_t n = GetModuleFileNameA(NULL, path, (DWORD)capacity);
    if (!n || n >= capacity) { path[0] = 0; return 0; }
#endif
    return (size_t)n;
}

uint32_t hos_host_thread_id(void) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return (uint32_t)syscall(SYS_gettid);
#else
    return GetCurrentThreadId();
#endif
}
int hos_yield(void) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return sched_yield() == 0;
#else
    return SwitchToThread();
#endif
}

typedef struct { void (*fn)(void *); void *arg; } Worker;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
static void *worker_main(void *p) {
#else
static DWORD WINAPI worker_main(void *p) {
#endif
    const Worker w = *(Worker *)p;
    free(p);
    w.fn(w.arg);
    return 0;
}
int hos_start_worker(void (*fn)(void *), void *arg) {
    Worker *w = (Worker *)malloc(sizeof *w);
    if (!w) return 0;
    w->fn = fn, w->arg = arg;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    const int ok = pthread_create(&thread, &attr, worker_main, w) == 0;
    pthread_attr_destroy(&attr);
#else
    HANDLE thread = CreateThread(NULL, 0, worker_main, w, 0, NULL);
    const int ok = thread != NULL;
    if (thread) CloseHandle(thread);
#endif
    if (!ok) free(w);
    return ok;
}

struct HosFiberContext {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    ucontext_t context;
    void *stack;
    size_t size;
    void (*fn)(void *);
    void *arg;
#else
    void *native;
    void (*fn)(void *);
    void *arg;
    int own_stack;
#endif
};
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
static void fiber_entry(unsigned low, unsigned high) {
    HosFiberContext *f = (HosFiberContext *)((uintptr_t)low | (uintptr_t)high << 32);
    f->fn(f->arg);
    abort();   /* The guest fiber must switch to its return target. */
}
#else
static void WINAPI fiber_entry(void *arg) {
    HosFiberContext *f = (HosFiberContext *)arg;
    f->fn(f->arg);
    abort();
}
#endif
HosFiberContext *hos_fiber_thread(void) {
    HosFiberContext *f = (HosFiberContext *)calloc(1, sizeof *f);
#if !defined(__linux__) || defined(__EMSCRIPTEN__)
    if (f && !(f->native = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH))) { free(f); return NULL; }
#endif
    return f;
}
HosFiberContext *hos_fiber_create(size_t stack_size, void (*fn)(void *), void *arg) {
    HosFiberContext *f = (HosFiberContext *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->fn = fn, f->arg = arg;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    if (!stack_size || stack_size > SIZE_MAX - page * 2) { free(f); return NULL; }
    const size_t usable = (stack_size + page - 1) / page * page;
    f->size = usable + page;
    f->stack = mmap(NULL, f->size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (f->stack == MAP_FAILED) { free(f); return NULL; }
    if (mprotect((char *)f->stack + page, usable, PROT_READ | PROT_WRITE) || getcontext(&f->context)) {
        munmap(f->stack, f->size); free(f); return NULL;
    }
    f->context.uc_stack.ss_sp = (char *)f->stack + page;
    f->context.uc_stack.ss_size = usable;
    f->context.uc_link = NULL;
    const uintptr_t p = (uintptr_t)f;
    makecontext(&f->context, (void (*)(void))fiber_entry, 2, (unsigned)p, (unsigned)(p >> 32));
#else
    f->own_stack = 1;
    f->native = CreateFiberEx(64 << 10, stack_size, FIBER_FLAG_FLOAT_SWITCH, fiber_entry, f);
    if (!f->native) { free(f); return NULL; }
#endif
    return f;
}
void hos_fiber_switch(HosFiberContext *from, HosFiberContext *to) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (swapcontext(&from->context, &to->context)) abort();
#else
    (void)from;
    SwitchToFiber(to->native);
#endif
}
void hos_fiber_destroy(HosFiberContext *f) {
    if (!f) return;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (f->stack) munmap(f->stack, f->size);
#else
    if (f->own_stack) DeleteFiber(f->native);
#endif
    free(f);
}
int hos_discard_memory(void *address, size_t size) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return madvise(address, size, MADV_DONTNEED) == 0;
#else
    return VirtualFree(address, size, MEM_DECOMMIT);
#endif
}
int hos_memory_readable(const void *address, size_t size) {
    const uintptr_t start = (uintptr_t)address;
    if (!size || size > UINTPTR_MAX - start) return !size;
    const uintptr_t end = start + size;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512], perms[8];
    uintptr_t next = start;
    while (next < end && fgets(line, sizeof line, maps)) {
        unsigned long low, high;
        if (sscanf(line, "%lx-%lx %7s", &low, &high, perms) != 3 || high <= next) continue;
        if (low > next || perms[0] != 'r') break;
        next = high;
    }
    fclose(maps);
    return next >= end;
#else
    uintptr_t next = start;
    while (next < end) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)next, &m, sizeof m) || m.State != MEM_COMMIT ||
            (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
        next = (uintptr_t)m.BaseAddress + m.RegionSize;
    }
    return 1;
#endif
}
int hos_memory_region(const void *address, HosMemoryRegion *region) {
    const uintptr_t a = (uintptr_t)address;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512], perms[8];
    int found = 0;
    while (fgets(line, sizeof line, maps)) {
        unsigned long low, high;
        if (sscanf(line, "%lx-%lx %7s", &low, &high, perms) != 3 || high <= a) continue;
        region->base = (void *)a;
        region->size = a < low ? low - a : high - a;
        region->readable = a >= low && perms[0] == 'r';
        region->writable = a >= low && perms[1] == 'w';
        found = 1;
        break;
    }
    fclose(maps);
    return found;
#else
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(address, &m, sizeof m)) return 0;
    region->base = m.BaseAddress;
    region->size = m.RegionSize;
    region->readable = m.State == MEM_COMMIT && !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    region->writable = region->readable && (m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE));
    return 1;
#endif
}
