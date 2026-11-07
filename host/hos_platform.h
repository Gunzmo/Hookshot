/* Small host services used by HLE code. No guest ABI types belong here. */
#ifndef HOS_PLATFORM_H
#define HOS_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
#define HOS_PATH_MAX 260
#else
#define HOS_PATH_MAX 4096
#endif
/* NUL-terminated executable path; 0 on failure or truncation. */
size_t hos_executable_path(char *path, size_t capacity);
uint32_t hos_host_thread_id(void);
int hos_yield(void);
/* Detached process-lifetime worker. Returns 0 if it could not be started. */
int hos_start_worker(void (*fn)(void *), void *arg);
typedef struct HosFiberContext HosFiberContext;
HosFiberContext *hos_fiber_thread(void);
HosFiberContext *hos_fiber_create(size_t stack_size, void (*fn)(void *), void *arg);
void hos_fiber_switch(HosFiberContext *from, HosFiberContext *to);
void hos_fiber_destroy(HosFiberContext *fiber);
/* Discard backing pages while retaining the reserved virtual addresses. */
int hos_discard_memory(void *address, size_t size);
int hos_memory_readable(const void *address, size_t size);
typedef struct { void *base; size_t size; int readable, writable; } HosMemoryRegion;
int hos_memory_region(const void *address, HosMemoryRegion *region);
#endif
