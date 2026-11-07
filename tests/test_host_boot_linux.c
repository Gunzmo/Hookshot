/* Native startup save migration uses the same implementation as the runtime. */
#undef A64_MEM_ZERO
#define main hookshot_boot_main
#include "../host/hos_main.c"
#undef main
#include <assert.h>
#include <sys/stat.h>

void hos_log(const char *fmt, ...) { (void)fmt; }
static void write_file(const char *path, const char *value) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(value, 1, strlen(value), f) == strlen(value)); assert(!fclose(f));
}
static void expect(const char *path, const char *value) {
    FILE *f = fopen(path, "rb"); assert(f);
    char buf[64] = {0}; assert(fread(buf, 1, sizeof buf, f) == strlen(value));
    assert(!memcmp(buf, value, strlen(value))); fclose(f);
}
int main(void) {
    char root[] = "/tmp/hookshot-boot-XXXXXX"; assert(mkdtemp(root));
    char old[4096], data[4096], path[4096], target[4096];
    snprintf(old, sizeof old, "%s/user", root); assert(!mkdir(old, 0700));
    snprintf(data, sizeof data, "%s/data", root); assert(!mkdir(data, 0700));
    assert(!setenv("XDG_DATA_HOME", data, 1));
    snprintf(path, sizeof path, "%s/slot", old); assert(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/slot/save.bin", old); write_file(path, "old-save");
    snprintf(target, sizeof target, "%s/hookshot", data);
    assert(!strcmp(saves_dir(old), target)); assert(fs_type(old) == -1);
    snprintf(path, sizeof path, "%s/slot/save.bin", target); expect(path, "old-save");
    /* A save at the destination wins; the original folder remains selected. */
    assert(!mkdir(old, 0700));
    snprintf(path, sizeof path, "%s/slot", old); assert(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/slot/save.bin", old); write_file(path, "keep-original");
    assert(!strcmp(saves_dir(old), old)); expect(path, "keep-original");
    snprintf(path, sizeof path, "%s/slot/save.bin", target); expect(path, "old-save");
    char source[4096], destination[4096];
    snprintf(source, sizeof source, "%s/source", root); write_file(source, "source");
    snprintf(destination, sizeof destination, "%s/destination", root); write_file(destination, "newer");
    assert(!fs_move_new(source, destination)); expect(source, "source"); expect(destination, "newer");
    /* Copying a directory refuses an existing destination. */
    assert(!copy_tree(old, target));
    /* Exercise the copy fallback on different filesystems when tmpfs is available. */
    struct stat a, b;
    if (!stat(root, &a) && !stat("/dev/shm", &b) && a.st_dev != b.st_dev && !access("/dev/shm", W_OK)) {
        char cross[] = "/dev/shm/hookshot-boot-XXXXXX"; assert(mkdtemp(cross));
        assert(!setenv("XDG_DATA_HOME", cross, 1));
        const char *selected = saves_dir(old); assert(strcmp(selected, old));
        snprintf(path, sizeof path, "%s/slot/save.bin", selected); expect(path, "keep-original");
        assert(fs_type(old) == -1); delete_tree(cross);
    }
    delete_tree(root); assert(fs_type(root) == -1);
    puts("native startup save moves, copies, collision preservation and cleanup passed");
    return 0;
}
