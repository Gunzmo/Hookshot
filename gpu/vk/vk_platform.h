#pragma once
#include <chrono>
#include <thread>
#include <cstdint>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <direct.h>
#else
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
extern "C" {
#include "../../host/hos_platform.h"
#include "../../host/hos_fault.h"
#include "../../host/hos_input.h"
}
#endif
#ifdef _WIN32
using AppWindow = HWND;
#else
using AppWindow = void*;
#endif
namespace vk_platform {
inline int seek(FILE *f, int64_t n, int whence) {
#ifdef _WIN32
    return _fseeki64(f,n,whence);
#else
    return fseeko(f,n,whence);
#endif
}
inline int64_t tell(FILE *f) {
#ifdef _WIN32
    return _ftelli64(f);
#else
    return ftello(f);
#endif
}
inline int truncate(FILE *f, int64_t n) {
#ifdef _WIN32
    return _chsize_s(_fileno(f),n);
#else
    return ftruncate(fileno(f),n);
#endif
}
inline int mkdir(const char *p) {
#ifdef _WIN32
    return _mkdir(p);
#else
    return ::mkdir(p,0777);
#endif
}
inline void sleep(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline int yield() { std::this_thread::yield(); return 1; }
inline uint64_t milliseconds() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline size_t executable(char *p, size_t n) {
#ifdef _WIN32
    return GetModuleFileNameA(nullptr, p, (DWORD)n);
#else
    return hos_executable_path(p,n);
#endif
}
inline bool exists(const char *p) {
#ifdef _WIN32
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat s; return stat(p, &s) == 0;
#endif
}
inline void name(const wchar_t *p) {
#ifdef _WIN32
    SetThreadDescription(GetCurrentThread(),p);
#else
    char s[16]; unsigned i=0; for (; i<15 && p[i]; ++i) s[i]=(char)p[i]; s[i]=0;
    pthread_setname_np(pthread_self(),s);
#endif
}
inline void priority(int level) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), level);
#else
    const int nice = -2*level;
    if (setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice) && nice<0)
        setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 0);
#endif
}
inline void affinity(uint64_t mask) {
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)mask);
#else
    cpu_set_t s; CPU_ZERO(&s); for(unsigned i=0;i<64;i++) if(mask&(1ull<<i)) CPU_SET(i,&s);
    pthread_setaffinity_np(pthread_self(), sizeof s, &s);
#endif
}
inline int key(int k) {
#ifdef _WIN32
    return GetAsyncKeyState(k);
#else
    return hos_linux_key_down(k) ? 0x8000 : 0;
#endif
}
inline bool focused() {
#ifdef _WIN32
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid); return pid==GetCurrentProcessId();
#else
    return hos_linux_has_focus();
#endif
}
inline bool replace(const char *from,const char *to) {
#ifdef _WIN32
    return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING);
#else
    return rename(from,to)==0;
#endif
}
}
