/* The common filesystem keeps its APFS save transaction implementation.
 * Game-specific file seeding is disabled with TOUCHABLE_SWITCH_RUNTIME. */
#include <hle_fs.c>
#include <sys/statvfs.h>

HLE_FORCE("stat", "Trace SDK file metadata queries", int, ss_posix_stat, const char *path, void *out) {
    int result = ((int (*)(const char *, void *))guest_sym("stat"))(path, out);
    if (g_flag.fs) {
        int error = *((int *(*)(void))guest_sym("__errno_location"))();
        uint32_t mode = 0; if (!result) memcpy(&mode, (uint8_t *)out + 16, 4);
        LOG("POSIX stat %s -> %d errno=%d mode=%#x", path, result, error, mode);
    }
    return result;
}

HLE_FORCE("_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE", "Host filesystem uses host allocation", void, ss_fs_allocator, void *allocate, void *release) { (void)allocate; (void)release; }
HLE_FORCE("_ZN2nn2fs25EnableGlobalFileDataCacheEPvm", "Host filesystem uses its page cache", Result, ss_fs_cache, void *buffer, size_t size) { (void)buffer; (void)size; return RES_OK; }
HLE_FORCE("_ZN2nn2fs9IsMountedEPKc", "Read the host mount table", unsigned, ss_fs_is_mounted, const char *name) {
    unsigned found = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; name && i < g_nmnt; ++i) if (!strcmp(name, g_mnt[i].name)) { found = 1; break; }
    pthread_mutex_unlock(&g_lock);
    return found;
}
/* SDK libc checks the volume before it calls GetEntryType or OpenFile. The
 * host mounts do not have SDK FileSystemAccessor objects, so all these queries
 * must use the same mount table as the file operations. */
HLE_FORCE("_ZN2nn2fs13IsPathMountedEPKc", "Check the volume of a guest path", unsigned, ss_fs_path_mounted, const char *path) {
    const char *colon = path ? strchr(path, ':') : NULL;
    if (!colon || colon == path || colon - path >= 32) return 0;
    char name[32]; size_t length = (size_t)(colon - path);
    memcpy(name, path, length); name[length] = 0;
    return ss_fs_is_mounted(name);
}
static Result ss_fs_path_limit(size_t *out, const char *path) {
    if (!ss_fs_path_mounted(path)) return 0x35f202u;
    /* The SDK clamps both UTF-8 limits to 0x300. */
    *out = 0x300;
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs16GetPathLengthMaxEPmPKc", "Get the guest path limit", Result, ss_fs_path_max, size_t *out, const char *path) { return ss_fs_path_limit(out, path); }
HLE_FORCE("_ZN2nn2fs21GetEntryNameLengthMaxEPmPKc", "Get the guest component limit", Result, ss_fs_name_max, size_t *out, const char *path) { return ss_fs_path_limit(out, path); }
static Result ss_fs_utf16_limit(uint8_t *defined, size_t *out, const char *path) {
    if (!ss_fs_path_mounted(path)) return 0x35f202u;
    /* These mounts use UTF-8 names and have no extra UTF-16 limit. */
    *defined = 0; *out = 0;
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs21GetUtf16PathLengthMaxEPbPmPKc", "Get the UTF-16 path limit", Result, ss_fs_utf16_path_max, uint8_t *defined, size_t *out, const char *path) { return ss_fs_utf16_limit(defined, out, path); }
HLE_FORCE("_ZN2nn2fs26GetUtf16EntryNameLengthMaxEPbPmPKc", "Get the UTF-16 component limit", Result, ss_fs_utf16_name_max, uint8_t *defined, size_t *out, const char *path) { return ss_fs_utf16_limit(defined, out, path); }
HLE_FORCE("_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE", "Count entries without changing the directory position", Result, ss_fs_dir_count, int64_t *out, uint64_t handle) {
    dirh_t *source = (dirh_t *)handle;
    dirh_t *scan = calloc(1, sizeof(*scan));
    DirectoryEntry *entries = calloc(32, sizeof(*entries));
    if (!scan || !entries) die("Cannot allocate directory count buffer");
    *scan = *source; scan->psnap = 0;
    if (!scan->snap) {
        scan->d = opendir(scan->path);
        if (!scan->d) { Result result = err_res("count directory", scan->path); free(entries); free(scan); return result; }
    }
    int64_t total = 0, count;
    Result result;
    do {
        result = h_fs_ReadDirectory(&count, entries, (uint64_t)scan, 32);
        if (result) break;
        total += count;
    } while (count);
    if (!scan->snap) closedir(scan->d);
    free(entries); free(scan);
    if (!result) *out = total;
    return result;
}
HLE_FORCE("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm", "Read a complete file range", Result, ss_fs_read_full, uint64_t file, int64_t offset, void *data, size_t size) {
    size_t got = 0; Result result = h_fs_ReadFile(&got, file, offset, data, size);
    return result ? result : got == size ? RES_OK : 0x1a02u;
}
HLE_FORCE("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvmRKNS0_10ReadOptionE", "Read a complete file range", Result, ss_fs_read_full_opt, uint64_t file, int64_t offset, void *data, size_t size, const void *option) {
    (void)option; return ss_fs_read_full(file, offset, data, size);
}
HLE_FORCE("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvmRKNS0_10ReadOptionE", "Read a file range", Result, ss_fs_read_opt, size_t *got, uint64_t file, int64_t offset, void *data, size_t size, const void *option) {
    (void)option; return h_fs_ReadFile(got, file, offset, data, size);
}
HLE_FORCE("_ZN2nn2fs15DeleteDirectoryEPKc", "Remove an empty directory", Result, ss_fs_delete_dir, const char *path) { HP(path); RW(); return rmdir(hp) ? err_res("rmdir", hp) : RES_OK; }
HLE_FORCE("_ZN2nn2fs15RenameDirectoryEPKcS2_", "Rename a directory", Result, ss_fs_rename_dir, const char *from, const char *to) { return h_fs_RenameFile(from, to); }
HLE_FORCE("_ZN2nn2fs16GetFreeSpaceSizeEPlPKc", "Read available storage", Result, ss_fs_free_space, int64_t *out, const char *path) {
    HP(path); struct statvfs status;
    if (statvfs(hp, &status)) return err_res("statvfs", hp);
    *out = (int64_t)status.f_bavail * status.f_frsize; return RES_OK;
}
HLE_FORCE("_ZN2nn2fs20GetRamDiskBufferSizeEm", "Temporary storage uses host files", size_t, ss_fs_ram_size, size_t size) { (void)size; return 4096; }
HLE_FORCE("_ZN2nn2fs12MountRamDiskEPKcPvm", "Temporary storage uses host files", Result, ss_fs_ram_mount, const char *name, void *data, size_t size) { (void)data; (void)size; return h_fs_MountTemp(name); }

/* SDK 14.3 also exports this entry point. Keep the same atomic transaction. */
HLE_FORCE("_ZN2nn2fs14CommitSaveDataEPKc", "Commit through the host save transaction", Result, ss_fs_commit_save, const char *name) {
    return h_fs_Commit(name);
}
