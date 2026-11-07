/* Host filesystem operations used by nn_fs.c. Linux uses file descriptors and directory streams;
   Windows keeps its existing file and directory APIs. */
#ifndef HOS_FS_PLATFORM_H
#define HOS_FS_PLATFORM_H

#if defined(_WIN32) || defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef SRWLOCK FsLock;
#define FS_LOCK_INIT SRWLOCK_INIT
#define fs_lock_read(l) AcquireSRWLockShared(l)
#define fs_unlock_read(l) ReleaseSRWLockShared(l)
#define fs_lock_write(l) AcquireSRWLockExclusive(l)
#define fs_unlock_write(l) ReleaseSRWLockExclusive(l)
#define fs_casecmp _stricmp
typedef HANDLE FsFile;
#define FS_INVALID_FILE INVALID_HANDLE_VALUE

typedef struct { HANDLE h; WIN32_FIND_DATAA data; int first; } FsDir;
static int fs_dir_open(FsDir *d, const char *path) {
    char pattern[260];
    snprintf(pattern, sizeof pattern, "%s\\*", path);
    d->h = FindFirstFileA(pattern, &d->data);
    d->first = d->h != INVALID_HANDLE_VALUE;
    return d->first;
}
static int fs_dir_next(FsDir *d, char *name, size_t cap, int *is_dir, uint64_t *size) {
    if (!d->first && !FindNextFileA(d->h, &d->data)) return 0;
    d->first = 0;
    snprintf(name, cap, "%s", d->data.cFileName);
    *is_dir = !!(d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    *size = (uint64_t)d->data.nFileSizeHigh << 32 | d->data.nFileSizeLow;
    return 1;
}
static void fs_dir_close(FsDir *d) { if (d->h != INVALID_HANDLE_VALUE) FindClose(d->h); }
static int fs_type(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a == INVALID_FILE_ATTRIBUTES ? -1 : !!(a & FILE_ATTRIBUTE_DIRECTORY);
}
/* mkdir/create: 0 success, 1 already exists, -1 other failure. */
static int fs_mkdir(const char *p) {
    return CreateDirectoryA(p, NULL) ? 0 : GetLastError() == ERROR_ALREADY_EXISTS ? 1 : -1;
}
static FsFile fs_open(const char *p, int write) {
    return CreateFileA(p, GENERIC_READ | (write ? GENERIC_WRITE : 0), FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}
static int fs_create(const char *p, int64_t size) {
    HANDLE h = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_EXISTS ? 1 : -1;
    LARGE_INTEGER sz; sz.QuadPart = size;
    SetFilePointerEx(h, sz, NULL, FILE_BEGIN);
    SetEndOfFile(h);
    CloseHandle(h);
    return 0;
}
static uint32_t fs_read(FsFile h, void *buf, uint32_t n, int64_t off) {
    OVERLAPPED ov = {0}; ov.Offset = (DWORD)off; ov.OffsetHigh = (DWORD)((uint64_t)off >> 32);
    DWORD got = 0; ReadFile(h, buf, n, &got, &ov); return got;
}
static uint32_t fs_write(FsFile h, const void *buf, uint32_t n, int64_t off) {
    OVERLAPPED ov = {0}; ov.Offset = (DWORD)off; ov.OffsetHigh = (DWORD)((uint64_t)off >> 32);
    DWORD put = 0; WriteFile(h, buf, n, &put, &ov); return put;
}
static uint64_t fs_size(FsFile h) { LARGE_INTEGER sz = {0}; GetFileSizeEx(h, &sz); return (uint64_t)sz.QuadPart; }
static void fs_resize(FsFile h, int64_t size) { LARGE_INTEGER sz; sz.QuadPart = size; SetFilePointerEx(h, sz, NULL, FILE_BEGIN); SetEndOfFile(h); }
static void fs_flush(FsFile h) { FlushFileBuffers(h); }
static void fs_close(FsFile h) { CloseHandle(h); }
static int fs_unlink(const char *p) { return DeleteFileA(p) != 0; }
static void fs_rmdir(const char *p) { RemoveDirectoryA(p); }
static int fs_rename(const char *a, const char *b) { return MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING) != 0; }
/* Save migration must never replace an entry already present at the destination. */
static int fs_move_new(const char *a, const char *b) { return MoveFileExA(a, b, 0) != 0; }
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef pthread_rwlock_t FsLock;
#define FS_LOCK_INIT PTHREAD_RWLOCK_INITIALIZER
#define fs_lock_read(l) pthread_rwlock_rdlock(l)
#define fs_unlock_read(l) pthread_rwlock_unlock(l)
#define fs_lock_write(l) pthread_rwlock_wrlock(l)
#define fs_unlock_write(l) pthread_rwlock_unlock(l)
#define fs_casecmp strcasecmp
typedef int FsFile;
#define FS_INVALID_FILE (-1)

typedef struct { DIR *dir; } FsDir;
static int fs_dir_open(FsDir *d, const char *path) { d->dir = opendir(path); return d->dir != NULL; }
static int fs_dir_next(FsDir *d, char *name, size_t cap, int *is_dir, uint64_t *size) {
    struct dirent *entry;
    struct stat st;
    while ((entry = readdir(d->dir))) {
        if (fstatat(dirfd(d->dir), entry->d_name, &st, 0)) continue;
        snprintf(name, cap, "%s", entry->d_name);
        *is_dir = S_ISDIR(st.st_mode);
        *size = (uint64_t)st.st_size;
        return 1;
    }
    return 0;
}
static void fs_dir_close(FsDir *d) { if (d->dir) closedir(d->dir); }
static int fs_type(const char *p) { struct stat st; return stat(p, &st) ? -1 : S_ISDIR(st.st_mode) != 0; }
static int fs_mkdir(const char *p) { return !mkdir(p, 0755) ? 0 : errno == EEXIST ? 1 : -1; }
static FsFile fs_open(const char *p, int write) {
    int fd = open(p, (write ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    struct stat st;
    if (fd >= 0 && !fstat(fd, &st) && S_ISDIR(st.st_mode)) { close(fd); return FS_INVALID_FILE; }
    return fd;
}
static int fs_create(const char *p, int64_t size) {
    int fd = open(p, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) return errno == EEXIST ? 1 : -1;
    (void)ftruncate(fd, (off_t)size);
    close(fd);
    return 0;
}
static uint32_t fs_read(FsFile fd, void *buf, uint32_t n, int64_t off) {
    ssize_t r; do { r = pread(fd, buf, n, (off_t)off); } while (r < 0 && errno == EINTR);
    return r > 0 ? (uint32_t)r : 0;
}
static uint32_t fs_write(FsFile fd, const void *buf, uint32_t n, int64_t off) {
    ssize_t r; do { r = pwrite(fd, buf, n, (off_t)off); } while (r < 0 && errno == EINTR);
    return r > 0 ? (uint32_t)r : 0;
}
static uint64_t fs_size(FsFile fd) { struct stat st; return fstat(fd, &st) ? 0 : (uint64_t)st.st_size; }
static void fs_resize(FsFile fd, int64_t size) { (void)ftruncate(fd, (off_t)size); }
static void fs_flush(FsFile fd) { (void)fsync(fd); }
static void fs_close(FsFile fd) { close(fd); }
static int fs_unlink(const char *p) { return unlink(p) == 0; }
static void fs_rmdir(const char *p) { (void)rmdir(p); }
static int fs_rename(const char *a, const char *b) { return rename(a, b) == 0; }
static int fs_move_new(const char *a, const char *b) { return renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_NOREPLACE) == 0; }
#endif

#define FS_PATH_MAX 260
#endif
