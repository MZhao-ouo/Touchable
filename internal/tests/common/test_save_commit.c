/* Exercise the SDK CommitSaveData alias through a real save transaction. */
#include "hle.h"
#include <assert.h>
#include <errno.h>
static void *commit_function;
#undef HLE_FORCE
#define HLE_FORCE(symbol, reason, ret, name, ...) \
    static ret name(__VA_ARGS__); \
    __attribute__((constructor)) static void register_##name(void) { \
        if (!strcmp(symbol,"_ZN2nn2fs14CommitSaveDataEPKc")) commit_function=(void *)name; \
    } \
    static ret name(__VA_ARGS__)
#include "../../runtime/common/ss_fs.c"
struct dm_flags g_flag;
uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_prof_add(int id,uint64_t ns,uint64_t bytes) { (void)id;(void)ns;(void)bytes; }
void dm_guest_write_prepare(void *p,size_t n) { (void)p;(void)n; }
void *guest_sym(const char *name) { (void)name;abort(); }
static void put(const char *path,const char *value) { FILE *f=fopen(path,"w");assert(f);assert(fwrite(value,1,3,f)==3);assert(!fclose(f)); }
static void check(const char *path,const char *value) { char data[4]={0};FILE *f=fopen(path,"r");assert(f);assert(fread(data,1,3,f)==3);fclose(f);assert(!strcmp(data,value)); }
int main(void) {
    Result (*commit)(const char *)=commit_function;
    assert(commit && "CommitSaveData must bypass console services");
    char root[]="/private/tmp/hayarigami-commit-XXXXXX";assert(mkdtemp(root));
    char save[512],work[512],file[600],working[600];
    snprintf(save,sizeof save,"%s/save",root);snprintf(work,sizeof work,"%s/save.work",root);
    assert(!mkdir(save,0755));snprintf(file,sizeof file,"%s/probe",save);put(file,"old");
    setenv("DM_SAVE",save,1);assert(!h_fs_MountSave("sd0",NULL));
    snprintf(working,sizeof working,"%s/probe",work);put(working,"new");check(file,"old");
    assert(!commit("sd0"));check(file,"new");
    put(working,"bad");atomic_store(&g_save_poison,ENOSPC);
    assert(commit("sd0")==RES_FAIL);check(file,"new");
    puts("PASS: CommitSaveData alias commits real files and retains prior save after failed transaction");
    return 0;
}
