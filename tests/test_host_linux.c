/* Native host contract checks: no Win32 headers or shim linked. */
#include "hos.h"
#include "hos_sync.h"
#include "hos_platform.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PTR(p) ((uint64_t)(uintptr_t)(p))
#define DECL(fn) void imp_##fn(Ctx *)
#define CALL(fn, ...) call(imp_##fn, (uint64_t[]){__VA_ARGS__}, sizeof((uint64_t[]){__VA_ARGS__}) / 8)
static uint64_t call(void (*fn)(Ctx *), const uint64_t *args, size_t n) {
    Ctx c = {0}; memcpy(c.x, args, n * 8); fn(&c); return c.x[0];
}
DECL(_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi);
DECL(_ZN2nn2os9LockMutexEPNS0_9MutexTypeE);
DECL(_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE);
DECL(_ZN2nn2os27InitializeConditionVariableEPNS0_21ConditionVariableTypeE);
DECL(_ZN2nn2os26TimedWaitConditionVariableEPNS0_21ConditionVariableTypeEPNS0_9MutexTypeENS_8TimeSpanE);
DECL(_ZN2nn2os23SignalConditionVariableEPNS0_21ConditionVariableTypeE);
DECL(_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE);
DECL(_ZN2nn2os11SignalEventEPNS0_9EventTypeE);
DECL(_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE);
DECL(_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE);
DECL(_ZN2nn2os22InitializeMessageQueueEPNS0_16MessageQueueTypeEPmm);
DECL(_ZN2nn2os16SendMessageQueueEPNS0_16MessageQueueTypeEm);
DECL(_ZN2nn2os19ReceiveMessageQueueEPmPNS0_16MessageQueueTypeE);
DECL(_ZN2nn2os15InitializeFiberEPNS0_9FiberTypeEPFS2_PvES3_S3_mi);
DECL(_ZN2nn2os13SwitchToFiberEPNS0_9FiberTypeE);
DECL(_ZN2nn2os13FinalizeFiberEPNS0_9FiberTypeE);
int hos_thread_ids(uint32_t *, const char **, int);
const uint64_t hos_thread_start_hook = 0, hos_thread_exit_hook = 0;
int hos_game_cores(void) { return 0; }
int hos_game_is_mkw(void) { return 0; }
void hos_backtrace(void) {}
void hos_setjmp_return(Ctx *c, uint64_t target) { (void)c; (void)target; }
int hos_nvn_event_value(uint64_t a, uint32_t *v) { (void)a; *v = 1; return 1; }
uint64_t recomp_cntpct(Ctx *c) { (void)c; return (uint64_t)hos_now_ns() / 32; }
uint64_t hos_sys_alloc(uint64_t size, uint64_t align) {
    void *p; assert(!posix_memalign(&p, align < sizeof(void *) ? sizeof(void *) : align, size));
    memset(p, 0, size); return PTR(p);
}

static uint8_t mutex_object[64], cond_object[64], queue_object[64], event_object[64], fiber_object[64];
static unsigned shared_count, fiber_phase;
static void guest(Ctx *A64_NOESC c) {
    const unsigned role = (unsigned)c->x[0];
    assert(hos_self() && c->thread == hos_self());
    assert(G32(hos_thread_guest(hos_self()) + 0x1b0) == hos_thread_handle(hos_self()));
    if (role == 1) {
        for (unsigned i = 1; i <= 1000; i++) {
            CALL(_ZN2nn2os9LockMutexEPNS0_9MutexTypeE, PTR(mutex_object));
            shared_count++;
            CALL(_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE, PTR(mutex_object));
            CALL(_ZN2nn2os16SendMessageQueueEPNS0_16MessageQueueTypeEm, PTR(queue_object), i);
        }
    } else if (role == 2) {
        for (unsigned i = 1; i <= 1000; i++) {
            uint64_t value = 0;
            CALL(_ZN2nn2os19ReceiveMessageQueueEPmPNS0_16MessageQueueTypeE, PTR(&value), PTR(queue_object));
            assert(value == i);
        }
    } else if (role == 3) {
        hos_thread_exit();   /* completion cleanup also runs for an early exit */
    } else if (role == 4) {
        int timeout;
        hos_thread_wait(hos_self(), -1, &timeout);
        assert(!timeout);
        hos_thread_wait(hos_self(), 2000000, &timeout);
        assert(timeout);
    } else if (role == 5) {
        CALL(_ZN2nn2os9LockMutexEPNS0_9MutexTypeE, PTR(mutex_object));
        shared_count = 123;
        CALL(_ZN2nn2os23SignalConditionVariableEPNS0_21ConditionVariableTypeE, PTR(cond_object));
        CALL(_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE, PTR(mutex_object));
    }
}
static void guest_fiber(Ctx *A64_NOESC c) {
    assert(fiber_phase == 0); fiber_phase = 1;
    c->x[0] = 0; imp__ZN2nn2os13SwitchToFiberEPNS0_9FiberTypeE(c);
    assert(fiber_phase == 2); fiber_phase = 3;
    c->x[0] = 0;   /* return to the thread's context */
}
A64Fn a64_lookup(uint64_t address) { return address == 10 ? guest_fiber : guest; }
void a64_icall(Ctx *c, uint64_t target) { a64_lookup(target)(c); }
static HosThread *make_thread(unsigned role) {
    return hos_thread_create(hos_sys_alloc(0x400, 16), 1, role, hos_sys_alloc(65536, 4096), 65536, 44, 0);
}
static void finish(HosThread *t) { hos_thread_join(t); hos_thread_join(t); hos_thread_destroy(t); }

static HosMutex worker_lock = HOS_MUTEX_INIT;
static HosCond worker_cv = HOS_COND_INIT;
static int worker_done;
static void worker(void *arg) {
    assert(arg == &worker_done);
    hos_mutex_lock(&worker_lock); worker_done = 1; hos_cond_signal(&worker_cv); hos_mutex_unlock(&worker_lock);
}
static void noop_signal(int sig) { (void)sig; }
static void *interrupt_sleep(void *arg) {
    usleep(2000); pthread_kill(*(pthread_t *)arg, SIGUSR1); return NULL;
}

/* A fiber may resume on a different OS thread; its host stack and FP environment survive. */
static HosFiberContext *migrating;
static _Thread_local HosFiberContext *root_fiber;
static int migrate_phase;
static void migrating_entry(void *unused) {
    (void)unused;
    int local = 37;
    migrate_phase = 1;
    hos_fiber_switch(migrating, root_fiber);
    assert(local == 37 && migrate_phase == 2);
    migrate_phase = 3;
    hos_fiber_switch(migrating, root_fiber);
    abort();
}
static void *resume_fiber(void *unused) {
    (void)unused;
    root_fiber = hos_fiber_thread(); assert(root_fiber);
    hos_fiber_switch(root_fiber, migrating);
    hos_fiber_destroy(root_fiber); return NULL;
}

/* A reader must be able to enter while another reader holds a table lock. */
static HosRwLock table_lock = HOS_RWLOCK_INIT;
static int second_reader;
static void *read_lock(void *arg) {
    (void)arg;
    hos_rwlock_read(&table_lock);
    __atomic_store_n(&second_reader, 1, __ATOMIC_RELEASE);
    hos_rwlock_read_unlock(&table_lock); return NULL;
}


typedef void (*HostFn)(Ctx *, void *);
uint64_t hos_hostcall(const char *, HostFn, void *);
uint64_t hos_hostcall_target(uint64_t, HostFn *, void **);
const char *hos_hostcall_name(uint64_t);
static void host_fn(Ctx *c, void *arg) { c->x[0] = (uintptr_t)arg; }
static void *publish_calls(void *arg) {
    for (int i = 0; i < 500; i++) {
        char name[64]; snprintf(name, sizeof name, "test-%u-%d", (unsigned)(uintptr_t)arg, i);
        const uint64_t address = hos_hostcall(name, host_fn, arg);
        assert(hos_hostcall(name, host_fn, arg) == address);
    }
    return NULL;
}
static void publication(void) {
    pthread_t writers[4];
    for (unsigned i = 0; i < 4; i++) assert(!pthread_create(&writers[i], NULL, publish_calls, (void *)(uintptr_t)(i + 1)));
    for (unsigned i = 0; i < 2000; i++) {
        HostFn fn = NULL; void *arg = NULL;
        const uint64_t address = 0x06000000ull + 16 * i;
        while (!hos_hostcall_target(address, &fn, &arg)) hos_yield();
        assert(fn == host_fn && (uintptr_t)arg >= 1 && (uintptr_t)arg <= 4);
        assert(hos_hostcall_name(address) && !strncmp(hos_hostcall_name(address), "test-", 5));
    }
    for (unsigned i = 0; i < 4; i++) pthread_join(writers[i], NULL);
}

int main(void) {
    publication();
    Ctx *main_ctx = hos_boot_main_thread(65536); assert(main_ctx);
    char path[4096]; assert(hos_executable_path(path, sizeof path) && path[0] == '/');
    char small[2] = "x"; assert(!hos_executable_path(small, sizeof small) && !small[0]);
    assert(hos_host_thread_id() > 0);
    hos_mutex_lock(&worker_lock);
    assert(hos_start_worker(worker, &worker_done));
    while (!worker_done) assert(hos_cond_wait(&worker_cv, &worker_lock, 1000));
    assert(!hos_cond_wait(&worker_cv, &worker_lock, 1));
    hos_mutex_unlock(&worker_lock);
    HosRecursiveMutex recursive = HOS_RECURSIVE_MUTEX_INIT;
    hos_recursive_lock(&recursive); hos_recursive_lock(&recursive);
    hos_recursive_unlock(&recursive); hos_recursive_unlock(&recursive);
    hos_rwlock_read(&table_lock);
    pthread_t reader; assert(!pthread_create(&reader, NULL, read_lock, NULL));
    const int64_t deadline = hos_now_ns() + 1000000000;
    while (!__atomic_load_n(&second_reader, __ATOMIC_ACQUIRE) && hos_now_ns() < deadline) hos_yield();
    assert(second_reader); hos_rwlock_read_unlock(&table_lock); pthread_join(reader, NULL);

    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    char *memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(memory != MAP_FAILED); memset(memory, 77, page * 2);
    assert(hos_memory_readable(memory, page * 2));
    assert(hos_discard_memory(memory, page) && !memory[0] && memory[page] == 77);
    assert(!mprotect(memory + page, page, PROT_NONE));
    assert(!hos_memory_readable(memory, page * 2));
    assert(hos_memory_readable(memory, page));
    HosMemoryRegion region; assert(hos_memory_region(memory + page, &region) && !region.readable);
    munmap(memory, page * 2);

    struct sigaction sa = {.sa_handler = noop_signal}; sigaction(SIGUSR1, &sa, NULL);
    pthread_t self = pthread_self(), interrupter;
    assert(!pthread_create(&interrupter, NULL, interrupt_sleep, &self));
    int64_t before = hos_now_ns(); hos_sleep_ns(10000000);
    assert(hos_now_ns() - before >= 10000000); pthread_join(interrupter, NULL);

    CALL(_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi, PTR(mutex_object), 1, 0);
    CALL(_ZN2nn2os27InitializeConditionVariableEPNS0_21ConditionVariableTypeE, PTR(cond_object));
    CALL(_ZN2nn2os22InitializeMessageQueueEPNS0_16MessageQueueTypeEPmm, PTR(queue_object), 0, 2);
    HosThread *producer = make_thread(1), *consumer = make_thread(2);
    hos_thread_start(producer); hos_thread_start(consumer); finish(producer); finish(consumer);
    assert(shared_count == 1000);
    HosThread *early = make_thread(3); hos_thread_start(early); finish(early);
    HosThread *wake = make_thread(4); hos_thread_wake(wake); hos_thread_start(wake); finish(wake);

    CALL(_ZN2nn2os9LockMutexEPNS0_9MutexTypeE, PTR(mutex_object));
    CALL(_ZN2nn2os9LockMutexEPNS0_9MutexTypeE, PTR(mutex_object));
    HosThread *signaller = make_thread(5); hos_thread_start(signaller);
    while (shared_count != 123) assert(CALL(_ZN2nn2os26TimedWaitConditionVariableEPNS0_21ConditionVariableTypeEPNS0_9MutexTypeENS_8TimeSpanE,
                                           PTR(cond_object), PTR(mutex_object), 1000000000));
    assert(!CALL(_ZN2nn2os26TimedWaitConditionVariableEPNS0_21ConditionVariableTypeEPNS0_9MutexTypeENS_8TimeSpanE,
                 PTR(cond_object), PTR(mutex_object), 1000000));
    CALL(_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE, PTR(mutex_object));
    CALL(_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE, PTR(mutex_object)); finish(signaller);
    CALL(_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE, PTR(event_object), 0, 1);
    assert(!CALL(_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE, PTR(event_object), 1000000));
    CALL(_ZN2nn2os11SignalEventEPNS0_9EventTypeE, PTR(event_object));
    assert(CALL(_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE, PTR(event_object)));
    assert(!CALL(_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE, PTR(event_object)));

    uint64_t fiber_stack = hos_sys_alloc(65536, 4096);
    CALL(_ZN2nn2os15InitializeFiberEPNS0_9FiberTypeEPFS2_PvES3_S3_mi, PTR(fiber_object), 10, 0, fiber_stack, 65536, 0);
    /* Use the live main context: SwitchToFiber distinguishes the thread context by its address. */
    main_ctx->x[0] = PTR(fiber_object); imp__ZN2nn2os13SwitchToFiberEPNS0_9FiberTypeE(main_ctx);
    assert(fiber_phase == 1); fiber_phase = 2;
    main_ctx->x[0] = PTR(fiber_object); imp__ZN2nn2os13SwitchToFiberEPNS0_9FiberTypeE(main_ctx);
    assert(fiber_phase == 3);
    CALL(_ZN2nn2os13FinalizeFiberEPNS0_9FiberTypeE, PTR(fiber_object));
    root_fiber = hos_fiber_thread(); migrating = hos_fiber_create(1 << 20, migrating_entry, NULL);
    assert(root_fiber && migrating); hos_fiber_switch(root_fiber, migrating); assert(migrate_phase == 1);
    migrate_phase = 2; pthread_t other; assert(!pthread_create(&other, NULL, resume_fiber, NULL)); pthread_join(other, NULL);
    assert(migrate_phase == 3); hos_fiber_destroy(migrating); hos_fiber_destroy(root_fiber);
    uint32_t ids[16]; const char *names[16];
    assert(hos_thread_ids(ids, names, 16) == 1 && ids[0] == hos_host_thread_id());
    puts("native host synchronization, threads, clocks, memory and fibers passed");
    return 0;
}
