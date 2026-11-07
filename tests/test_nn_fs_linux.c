/* Game-free Linux nn::fs regression. Run with tests/test_nn_fs_linux.py. */
#include "hos.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PTR(p) ((uint64_t)(uintptr_t)(p))
#define CALL(fn, ...) do { Ctx c = {0}; uint64_t args[] = {__VA_ARGS__}; \
    memcpy(c.x, args, sizeof args); imp_##fn(&c); result = c.x[0]; } while (0)
#define DECL(fn) void imp_##fn(Ctx *)
DECL(_ZN2nn2fs21MountTemporaryStorageEPKc);
DECL(_ZN2nn2fs8MountRomEPKc);
DECL(_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE);
DECL(_ZN2nn2fs10CreateFileEPKcl);
DECL(_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci);
DECL(_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE);
DECL(_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm);
DECL(_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE);
DECL(_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl);
DECL(_ZN2nn2fs9CloseFileENS0_10FileHandleE);
DECL(_ZN2nn2fs15CreateDirectoryEPKc);
DECL(_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc);
DECL(_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci);
DECL(_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl);
DECL(_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE);
DECL(_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE);
DECL(_ZN2nn2fs10RenameFileEPKcS2_);
DECL(_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc);
extern const char *hos_user_dir, *hos_romfs_dir;
int hos_trace;
void hos_log(const char *fmt, ...) { (void)fmt; }
void hos_fatal(Ctx *c, const char *fmt, ...) { (void)c; (void)fmt; abort(); }
int hos_vcore_away(void) { return 0; }
void hos_vcore_back(void) {}
int64_t hos_now_ns(void) { return 0; }
void hos_sleep_ns(int64_t ns) { (void)ns; }
int hos_mods_romfs(const char *rel, char *out, size_t cap) { (void)rel; (void)out; (void)cap; return 0; }
int hos_mods_dir_extras(const char *rel, const char *base, void (*fn)(void *, const char *, int, uint64_t), void *arg) {
    (void)base;
    if (!strcmp(rel, "mods")) { fn(arg, "extra", 0, 42); return 1; }
    return 0;
}
int hos_blank_cached_icon(const char *g, const char *h) { (void)g; (void)h; return 0; }
int hos_ultrawide_redirect(const char *g, char *h, size_t n) { (void)g; (void)h; (void)n; return 0; }

static void *reader(void *arg) {
    uint64_t file = *(uint64_t *)arg, count, result;
    char buf[5];
    for (int i = 0; i < 1000; i++) {
        memset(buf, 0, sizeof buf);
        CALL(_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm, PTR(&count), file, 2, PTR(buf), 4);
        assert(!result && count == 4 && !memcmp(buf, "cdef", 4));
    }
    return NULL;
}
int main(void) {
    char root[] = "/tmp/hookshot-fs-XXXXXX";
    assert(mkdtemp(root));
    hos_user_dir = root;
    hos_romfs_dir = root;
    uint64_t result, file = 0, dir = 0, count = 0, size = 0;
    const char *mount = "tmp", *rom = "rom", *save = "save";
    const char *file_path = "tmp:/folder/file", *folder = "tmp:/folder";
    CALL(_ZN2nn2fs21MountTemporaryStorageEPKc, PTR(mount)); assert(!result);
    CALL(_ZN2nn2fs8MountRomEPKc, PTR(rom)); assert(!result);
    char resolved[260], expected[260];
    assert(hos_fs_host_path(file_path, resolved, sizeof resolved));
    snprintf(expected, sizeof expected, "%s/temp/folder/file", root);
    assert(!strcmp(resolved, expected));
    CALL(_ZN2nn2fs10CreateFileEPKcl, PTR("rom:/bad"), 0); assert(result == 0x202);
    snprintf(expected, sizeof expected, "%s/asset", root);
    FILE *asset = fopen(expected, "wb"); assert(asset && fwrite("rom", 1, 3, asset) == 3 && !fclose(asset));
    snprintf(expected, sizeof expected, "%s/overlay", root); assert(!mkdir(expected, 0755));
    assert(!setenv("HOS_FS_OVERLAY", expected, 1));
    snprintf(expected, sizeof expected, "%s/overlay/asset", root);
    asset = fopen(expected, "wb"); assert(asset && fwrite("overlay", 1, 7, asset) == 7 && !fclose(asset));
    CALL(_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci, PTR(&file), PTR("rom:/asset"), 1); assert(!result);
    char overlay[8] = {0};
    CALL(_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm, PTR(&count), file, 0, PTR(overlay), 7);
    assert(!result && count == 7 && !strcmp(overlay, "overlay"));
    CALL(_ZN2nn2fs9CloseFileENS0_10FileHandleE, file);
    assert(!unsetenv("HOS_FS_OVERLAY"));
    assert(!unlink(expected));
    snprintf(expected, sizeof expected, "%s/overlay", root); assert(!rmdir(expected));
    snprintf(expected, sizeof expected, "%s/asset", root); assert(!unlink(expected));
    CALL(_ZN2nn2fs10CreateFileEPKcl, PTR(file_path), 8); assert(!result);
    CALL(_ZN2nn2fs10CreateFileEPKcl, PTR(file_path), 8); assert(result == 0x402);
    CALL(_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci, PTR(&file), PTR(file_path), 3); assert(!result && file);
    CALL(_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE, file, 0, PTR("abcdefgh"), 8, 0); assert(!result);
    CALL(_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE, PTR(&size), file); assert(!result && size == 8);
    char initial[4];
    CALL(_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm, PTR(&count), file, 2, PTR(initial), 4);
    assert(!result && count == 4 && !memcmp(initial, "cdef", 4));
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, reader, &file));
    for (int i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
    CALL(_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl, file, 4); assert(!result);
    CALL(_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE, PTR(&size), file); assert(!result && size == 4);
    CALL(_ZN2nn2fs9CloseFileENS0_10FileHandleE, file);
    CALL(_ZN2nn2fs15CreateDirectoryEPKc, PTR("tmp:/folder/sub")); assert(!result);
    uint32_t type = 99;
    CALL(_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc, PTR(&type), PTR(folder)); assert(!result && type == 0);
    CALL(_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc, PTR(&type), PTR(file_path)); assert(!result && type == 1);
    CALL(_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc, PTR(&type), PTR("tmp:/missing")); assert(result == 0x202);
    CALL(_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci, PTR(&dir), PTR(folder), 3); assert(!result);
    CALL(_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE, PTR(&count), dir); assert(!result && count == 2);
    _Alignas(uint64_t) unsigned char entry[0x310]; int seen = 0;
    for (int i = 0; i < 2; i++) {
        CALL(_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl, PTR(&count), PTR(entry), dir, 1);
        assert(!result && count == 1);
        if (!strcmp((char *)entry, "file")) { assert(entry[0x302] == 1 && *(uint64_t *)(entry + 0x308) == 4); seen |= 1; }
        if (!strcmp((char *)entry, "sub")) { assert(entry[0x302] == 0); seen |= 2; }
    }
    assert(seen == 3);
    CALL(_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl, PTR(&count), PTR(entry), dir, 1); assert(!result && count == 0);
    CALL(_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE, PTR(&count), dir); assert(!result && count == 2);
    CALL(_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE, dir);
    CALL(_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci, PTR(&dir), PTR("rom:/mods"), 2); assert(!result);
    CALL(_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl, PTR(&count), PTR(entry), dir, 1);
    assert(!result && count == 1 && !strcmp((char *)entry, "extra") && *(uint64_t *)(entry + 0x308) == 42);
    CALL(_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE, dir);
    CALL(_ZN2nn2fs10RenameFileEPKcS2_, PTR(file_path), PTR("tmp:/folder/renamed")); assert(!result);
    CALL(_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc, PTR(folder)); assert(!result);
    assert(hos_fs_host_path(file_path, expected, sizeof expected));
    assert(access(expected, F_OK) != 0);
    uint64_t uid[2] = {1, 2};
    CALL(_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE, PTR(save), PTR(uid)); assert(!result);
    snprintf(expected, sizeof expected, "%s/save/00000000000000020000000000000001", root);
    struct stat st; assert(!stat(expected, &st) && S_ISDIR(st.st_mode));
    assert(!rmdir(expected));
    snprintf(expected, sizeof expected, "%s/save", root); assert(!rmdir(expected));
    snprintf(expected, sizeof expected, "%s/temp", root); assert(!rmdir(expected));
    assert(!rmdir(root));
    return 0;
}
