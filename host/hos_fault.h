#ifndef HOS_FAULT_H
#define HOS_FAULT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Linux fault hooks: return 1 only after repairing the faulting access. */
typedef struct { int signal, write; uintptr_t address, pc; void *context; } HosFault;
typedef int (*HosFaultHandler)(HosFault *);
int hos_fault_add(HosFaultHandler handler);
void hos_fault_reporter(HosFaultHandler handler);
int hos_guarded_copy(void *dst, const void *src, size_t size);
#ifdef __cplusplus
}
#endif
#endif
