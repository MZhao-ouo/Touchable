/* Sprite batcher waits for main Build ID 03ff4a3bec96bde3. Check its code and vtables before use.

   The sprite batcher (Begin main+0x50e6f0, End main+0x50e800, Present main+0x511130) holds 300
   sprites. Begin and each flush (main+0x510af0) call nvnQueueFinish on the batcher queue
   (this+0x3690) and nvnSyncWait on the sync of command buffer W (W = this+0x56c0, sync W+0x118).
   Each of these game wrappers keeps a pointer to its NVN object at offset 8.
   BeatSpell battles flush about 45 times in each frame. Thus the CPU waits for the GPU about 45
   times in each frame.

   The waits protect these areas:
   - Before each submit, main+0x50e0f0 copies the sprites into uniform buffer B1 (this+0x5848).
     It also copies the shared constants into uniform buffer B2 (this+0x5850) when bit 1 of
     this+0x1266c is set. Inline uniform updates (main+0x50f180 and others) also change B2.
     mtl_copy_on_submit makes each submit read its own copy of B1 and B2. The Metal layer makes
     the copy when it encodes the submit, and it applies the inline updates to the copy.
   - W records into the same command memory after each submit. The Metal layer reads the commands
     when it encodes the submit.
   This adapter skips these waits between Begin and End on the same thread. Present keeps its
   finish, so the CPU stays at most one frame ahead of the GPU.
   Both copies require the Metal layer to encode each submit inside the submit call. The adapter
   sets DM_NVN_SYNC=1, so the NVN layer translates on the game thread and not on its translation
   thread.

   RH_BATCH_WAIT=1 keeps all waits. RH_BATCH_TRACE=1 logs the counts every 600 Begin calls. */
#include "rh_batch.h"
#include "nvn.h"
#include <stdatomic.h>

void *nvn_hook(const char *name, void *function);
int mtl_copy_on_submit(uint64_t address, uint64_t size);
void mtl_copy_on_submit_stats(uint64_t *copies, uint64_t *bytes);

typedef void (*method)(void *, uint64_t, uint64_t, uint64_t);
static uint8_t *base;
static method begin, end, present;
static void (*queue_finish)(NVNqueue *);
static NVNsyncWaitResult (*sync_wait)(const NVNsync *, uint64_t);
static __thread uint8_t *active;   /* the batcher between Begin and End on this thread */
static int trace;
static uint64_t copied_address[2], copied_size[2];
static _Atomic uint64_t begins, finish_skipped, wait_skipped;

static void *ptr(const void *p, unsigned offset) { return *(void *const *)((const uint8_t *)p + offset); }
static uint32_t word(const void *p, unsigned offset) { return *(const uint32_t *)((const uint8_t *)p + offset); }
static uint64_t count(_Atomic uint64_t *value) { return atomic_load_explicit(value, memory_order_relaxed); }
static void add(_Atomic uint64_t *value) { atomic_fetch_add_explicit(value, 1, memory_order_relaxed); }

static void finish_hook(NVNqueue *queue) {
    uint8_t *batcher = active;
    if (batcher && queue == ptr(batcher, 0x3698)) { add(&finish_skipped); return; }
    queue_finish(queue);
}

static NVNsyncWaitResult wait_hook(const NVNsync *sync, uint64_t timeout) {
    uint8_t *batcher = active;
    if (batcher && sync == ptr(ptr(batcher, 0x56c0), 0x120)) {
        add(&wait_skipped);
        return NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED;
    }
    return sync_wait(sync, timeout);
}

/* Return 1 when the Metal layer copies uniform buffer B1 (index 0) or B2 (index 1) on each submit. */
static int copy_on_submit(uint8_t *batcher, int index) {
    uint8_t *buffer = ptr(batcher, 0x5848 + 8 * index);
    uint64_t address = 0, size = *(const uint64_t *)(buffer + 0x58);
    ((void (*)(void *, uint64_t *, int))(base + 0x50b050))(buffer, &address, 0);
    if (address == copied_address[index] && size == copied_size[index]) return 1;
    if (!address || !size || !mtl_copy_on_submit(address, size)) {
        LOG("【红】Sprite batcher: cannot copy B%d %#llx size %#llx on submit; keep the waits", index + 1,
            (unsigned long long)address, (unsigned long long)size);
        return 0;
    }
    copied_address[index] = address; copied_size[index] = size;
    LOG("Sprite batcher: copy B%d %#llx size %#llx stride %#llx on submit", index + 1, (unsigned long long)address,
        (unsigned long long)size, (unsigned long long)*(const uint64_t *)(buffer + 0x60));
    return 1;
}

static void begin_hook(void *self, uint64_t a1, uint64_t a2, uint64_t a3) {
    active = copy_on_submit(self, 0) && copy_on_submit(self, 1) ? self : NULL;
    if (trace && count(&begins) % 600 == 0) {
        uint64_t copies, bytes; mtl_copy_on_submit_stats(&copies, &bytes);
        LOG("Sprite batcher: begins %llu finish skipped %llu wait skipped %llu buffer copies %llu bytes %llu",
            (unsigned long long)count(&begins), (unsigned long long)count(&finish_skipped), (unsigned long long)count(&wait_skipped),
            (unsigned long long)copies, (unsigned long long)bytes);
    }
    add(&begins);
    begin(self, a1, a2, a3);
}

static void end_hook(void *self, uint64_t a1, uint64_t a2, uint64_t a3) {
    end(self, a1, a2, a3);
    if (active == self) active = NULL;
}

static void present_hook(void *self, uint64_t a1, uint64_t a2, uint64_t a3) {
    active = NULL;
    present(self, a1, a2, a3);
}

void rh_batch_install(const module_t *main) {
    base = main->base;
    trace = getenv("RH_BATCH_TRACE") != NULL;
    const char *keep = getenv("RH_BATCH_WAIT");
    if (keep && *keep && strcmp(keep, "0")) { LOG("Sprite batcher: keep all GPU waits (RH_BATCH_WAIT)"); return; }
    static const struct { unsigned offset; uint32_t word; } code[] = {
        {0x50e6f0, 0xd10103ff}, {0x50e7b0, 0x8b150260}, {0x50e7b4, 0x9415504f}, {0x50e7dc, 0x94000719},
        {0x510470, 0x97ffec68}, {0x50e800, 0xa9be7bfd}, {0x50e810, 0x940008b8}, {0x511130, 0xd10203ff},
        {0x51118c, 0x97fff545}, {0x510b0c, 0xf96b6014}, {0x510b74, 0x97fff6cb}, {0x510b7c, 0x97fff55d},
        {0x510ba4, 0x97ffea9b}, {0x510bf8, 0x97fff6aa}, {0x510c28, 0x97ffea7a}, {0x50e6b8, 0x5286d208},
        {0x50e6c0, 0x8b080260}, {0x50e6c4, 0x9415508b}, {0x50b62c, 0x91046260}, {0x50b638, 0x94156726},
        {0x50b050, 0xa9bd7bfd}, {0x50b074, 0xf94032a9}, {0x50e108, 0xf96c2400}, {0x50e1dc, 0xf96c2660},
        {0x50e1e4, 0x5284cd88}, {0x50e1f0, 0x370800c9}, {0x50e208, 0xf96c2a60}, {0xa628f8, 0xf9400400},
        {0xa652dc, 0xf9400400}, {0xa5cae0, 0xf9400400}, {0x50f198, 0xf96c2815}, {0x50f20c, 0xf9402ea2},
    };
    for (unsigned i = 0; i < sizeof code / sizeof code[0]; ++i)
        if (code[i].offset + 4 > main->text_end || word(base, code[i].offset) != code[i].word) {
            LOG("【红】Sprite batcher: code does not match at main+%#x; keep the GPU waits", code[i].offset);
            return;
        }
    const struct { unsigned slot, function; } expected[] = {{0xd57798, 0x50e6f0}, {0xd577a0, 0x50e800}, {0xd57848, 0x511130}};
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; ++i)
        if (main->data_off > expected[i].slot || main->patch_off < expected[i].slot + 8 || ptr(base, expected[i].slot) != base + expected[i].function) {
            LOG("【红】Sprite batcher: vtable does not match at main+%#x; keep the GPU waits", expected[i].slot);
            return;
        }
    begin = (method)(base + 0x50e6f0); end = (method)(base + 0x50e800); present = (method)(base + 0x511130);
    unsigned slots = 0;
    for (uintptr_t offset = main->data_off; offset + 8 <= main->patch_off; offset += 8) {
        void **slot = (void **)(base + offset);
        if (*slot == (void *)begin) { *slot = begin_hook; ++slots; }
        else if (*slot == (void *)end) { *slot = end_hook; ++slots; }
        else if (*slot == (void *)present) { *slot = present_hook; ++slots; }
    }
    queue_finish = nvn_hook("nvnQueueFinish", finish_hook);
    sync_wait = nvn_hook("nvnSyncWait", wait_hook);
    /* The NVN layer reads this at the first submit. */
    setenv("DM_NVN_SYNC", "1", 1);
    LOG("Sprite batcher: skip GPU waits between Begin and End (%u method slots); translate submits on the game thread", slots);
}
