/* Rhythm Heaven touch hints.
 *
 * A minigame shows its title card from graph/prologue/<name>/ and plays from graph/scene/<name>/.
 * Menus read graph/menu/ and results graph/result/; there the phone shows a small D-pad.
 * The phone uses a compact D-pad for two or more directions. A remix
 * reads several scene folders after its title card, so the keys of every scene read since the
 * last title card are combined.
 *
 * The table lists the buttons each minigame uses besides A. It comes from the button icons in the
 * controller prompts of mesg/CN/CNzh/stage_<name>_00.msbt (labels without the "H" suffix, which
 * describe a single sideways Joy-Con). Y (hold for a demo), - (skip practice) and + (pause) are
 * not listed: the phone has separate buttons for them.
 *
 * BeatSpell reads graph/beatspell/. Its battles step with B or down, cast with A or right, and do a
 * backflip with left or Y (mesg/CN/CNzh/beatspell_common.msbt). Its map and lists select with up
 * and down, open the items with X and open the map with Y. The left area presses B, the most
 * frequent button after A. The phone also shows the small D-pad and X and Y buttons there.
 *
 * Drum Lessons read graph/scene/drummer/. A lesson uses almost every button, so the phone shows a full
 * controller there and no touch area. The Rhythm Toys take taps on their items (rh_menu.c). */
#include "rh_touch.h"
#include "ss_touch.h"
#include <string.h>

enum { K_A = 1, K_B = 2, K_LEFT = 4, K_UP = 8, K_RIGHT = 16, K_DOWN = 32, K_MANY = 64, K_SPELL = 128, K_PAD = 256 };
typedef struct { const char *name; unsigned keys; } scene_keys;
static const scene_keys scenes[] = {
    {"birds", K_A}, {"block", K_A}, {"cooking", K_A}, {"flydisc", K_A}, {"fugu", K_A}, {"grasp", K_A},
    {"guardian", K_A}, {"hammer", K_A}, {"hungry", K_A}, {"insect", K_A}, {"kaeru", K_A}, {"kukan", K_A},
    {"moon", K_A}, {"pumpup", K_A}, {"putilabo", K_A}, {"rabbit", K_A}, {"ring", K_A}, {"rock", K_A},
    {"rocket", K_A}, {"rope", K_A}, {"samurai", K_A}, {"shinkai", K_A}, {"shooting", K_A}, {"snack", K_A},
    {"spinning", K_A}, {"teatime", K_A}, {"tennis", K_A}, {"the_a", K_A}, {"thunder", K_A}, {"virus", K_A},
    {"volley", K_A},
    {"bacteria", K_A | K_DOWN}, {"blanco", K_A | K_DOWN}, {"cars", K_A | K_DOWN}, {"hurdle", K_A | K_DOWN},
    {"owl", K_A | K_DOWN}, {"parasol", K_A | K_DOWN}, {"skater", K_A | K_DOWN}, {"softcatch", K_A | K_DOWN},
    {"brush", K_A | K_LEFT}, {"clap", K_A | K_LEFT}, {"commander", K_A | K_LEFT}, {"crab", K_A | K_LEFT},
    {"onion", K_A | K_LEFT}, {"soccer", K_A | K_LEFT}, {"talk", K_A | K_LEFT}, {"tono2", K_A | K_LEFT},
    {"trolley", K_A | K_LEFT}, {"wiper", K_A | K_LEFT},
    {"rolling", K_A | K_RIGHT},
    {"dancer", K_A | K_LEFT | K_RIGHT}, {"master", K_A | K_LEFT | K_RIGHT},
    {"foot_race", K_A | K_B}, {"quiz", K_A | K_B},
    /* More buttons than two touch areas hold, or no prompts to read: the left half stays a D-pad. */
    {"audio_tile", K_MANY}, {"konkon", K_MANY}, {"nervous", K_MANY},
    {"doc_comic", K_MANY}, {"doc_text", K_MANY},
    /* Drum Lessons use A, B, X, Y, the four directions, ZR, and - (mesg/CN/CNzh/stage_drummer_00.msbt). */
    {"drummer", K_PAD},
    {"remix", 0},   /* adds no keys itself; its scenes do */
};

static unsigned keys_of(const char *name, size_t length) {
    for (size_t i = 0; i < sizeof scenes / sizeof *scenes; ++i)
        if (strlen(scenes[i].name) == length && !memcmp(scenes[i].name, name, length)) return scenes[i].keys;
    return K_MANY;
}

static rh_touch_hint hint_of(const rh_touch_state *state) {
    rh_touch_hint dpad = {SS_TOUCH_DPAD, 0, 0, 0};
    if (!state->active) return (rh_touch_hint){SS_TOUCH_MENU, 0, 0, 0};
    if (state->keys & K_PAD) return (rh_touch_hint){SS_TOUCH_PAD, 0, 0, 0};
    if (state->keys & K_SPELL)
        return (rh_touch_hint){SS_TOUCH_ONE, SS_BUTTON_B, 0, SS_TOUCH_SHOW_DPAD | SS_TOUCH_SHOW_X | SS_TOUCH_SHOW_Y};
    if (state->keys & K_MANY) return dpad;
    unsigned directions = state->keys & (K_LEFT | K_UP | K_RIGHT | K_DOWN);
    dpad.first = (directions & K_LEFT ? SS_TOUCH_DIR_LEFT : 0) |
                 (directions & K_UP ? SS_TOUCH_DIR_UP : 0) |
                 (directions & K_RIGHT ? SS_TOUCH_DIR_RIGHT : 0) |
                 (directions & K_DOWN ? SS_TOUCH_DIR_DOWN : 0);
    if (directions && (directions & (directions - 1))) return dpad;
    /* Order of the left-half areas from left to right. */
    static const struct { unsigned key; int button; } order[] = {
        {K_LEFT, SS_BUTTON_LEFT}, {K_B, SS_BUTTON_B}, {K_DOWN, SS_BUTTON_DOWN}, {K_UP, SS_BUTTON_UP}, {K_RIGHT, SS_BUTTON_RIGHT}};
    int found[5], count = 0;
    for (int i = 0; i < 5; ++i) if (state->keys & order[i].key) found[count++] = order[i].button;
    if (count == 0) return state->keys & K_A ? (rh_touch_hint){SS_TOUCH_ONE, SS_BUTTON_A, 0, 0} : dpad;
    if (count == 1) return (rh_touch_hint){SS_TOUCH_ONE, found[0], 0, 0};
    if (count == 2) return (rh_touch_hint){SS_TOUCH_TWO, found[0], found[1], 0};
    return dpad;
}

int rh_touch_loading(const rh_touch_state *state, const char *path) {
    const char *graph = strstr(path, ":/graph/");
    if (!graph) return 0;
    return !(state->active && !strncmp(graph + 8, "scene/", 6));
}

int rh_touch_path(rh_touch_state *state, const char *path, rh_touch_hint *out) {
    const char *graph = strstr(path, ":/graph/");
    if (!graph) return 0;
    const char *top = graph + 8, *slash = strchr(top, '/');
    if (!slash) return 0;
    size_t top_length = (size_t)(slash - top);
    const char *name = slash + 1, *end = strchr(name, '/');
    size_t name_length = end ? (size_t)(end - name) : 0;
    rh_touch_hint before = hint_of(state);
#define TOP(text) (top_length == sizeof text - 1 && !memcmp(top, text, top_length))
#define NAME(text) (name_length == sizeof text - 1 && !memcmp(name, text, name_length))
    if (TOP("menu") || TOP("result") || TOP("epilogue") || TOP("boot")) {
        state->active = 0; state->keys = 0;
    } else if (TOP("beatspell")) {
        state->active = 1; state->keys = K_SPELL;
    } else if (TOP("prologue") && name_length && !NAME("common")) {
        state->active = 1; state->keys = keys_of(name, name_length);
    } else if (TOP("scene") && name_length && !NAME("common") && !NAME("multi_common")) {
        if (!state->active) { state->active = 1; state->keys = 0; }
        state->keys |= keys_of(name, name_length);
    } else {
        return 0;
    }
#undef TOP
#undef NAME
    rh_touch_hint after = hint_of(state);
    if (after.layout == before.layout && after.first == before.first && after.second == before.second &&
        after.extra == before.extra) return 0;
    *out = after;
    return 1;
}
