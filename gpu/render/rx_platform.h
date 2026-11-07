#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#include <strings.h>
#include <dlfcn.h>
#include <x86intrin.h>
extern "C" {
#include "../../host/hos_platform.h"
}
#else
#include <windows.h>
#include <intrin.h>
#endif
namespace rx_platform {
inline void directory(const char *p) {
    std::error_code error;
    std::filesystem::create_directory(p,error);
}
inline bool replace(const char *from,const char *to) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return std::rename(from,to)==0;
#else
    return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING);
#endif
}
inline void *allocate(size_t size) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    void *p = mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    return p == MAP_FAILED ? nullptr : p;
#else
    return VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#endif
}
inline void release(void *p,size_t size) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    munmap(p,size);
#else
    (void)size; VirtualFree(p,0,MEM_RELEASE);
#endif
}
inline bool reset(void *p,size_t size) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return madvise(p,size,MADV_DONTNEED)==0;
#else
    return !VirtualFree(p,size,MEM_DECOMMIT) || VirtualAlloc(p,size,MEM_COMMIT,PAGE_READWRITE);
#endif
}
inline int casecmp(const char *a,const char *b) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return strcasecmp(a,b);
#else
    return _stricmp(a,b);
#endif
}
}
