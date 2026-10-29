/* Build: xcrun clang -Wall -Wextra -Iruntime/common -Igames/rhythm-heaven/runtime games/rhythm-heaven/tests/test_touch.c
 *        games/rhythm-heaven/runtime/rh_touch.c -o build/checks/rhythm-heaven/test_touch && build/checks/rhythm-heaven/test_touch */
#include <assert.h>
#include <stdio.h>
#include "rh_touch.h"
#include "ss_touch.h"

static rh_touch_state state;
static rh_touch_hint hint = {SS_TOUCH_MENU, 0, 0, 0};

static void open_path(const char *path) { rh_touch_path(&state, path, &hint); }
static void expect(int layout, int first, int second) {
    assert(hint.layout == layout);
    assert(hint.first == first);
    assert(hint.second == second);
    assert(hint.extra == 0);
}

int main(void) {
    rh_touch_hint out;
    /* Graph archives of a new screen mark a load. Scene archives during a level do not. */
    assert(!rh_touch_loading(&state, "data:/mesg/CN/CNzh/stage_parasol_00.msbt"));
    assert(rh_touch_loading(&state, "data:/graph/menu/main_menu/main.spr"));
    assert(rh_touch_loading(&state, "data:/graph/scene/common/pause.spr"));
    rh_touch_state level = {1, 0};
    assert(!rh_touch_loading(&level, "data:/graph/scene/ring/type_00/main.spr"));
    assert(rh_touch_loading(&level, "data:/graph/prologue/wiper/type_00/prologue.spr"));
    assert(rh_touch_loading(&level, "data:/graph/result/result.spr"));
    assert(rh_touch_loading(&level, "data:/graph/beatspell/common/common.spr"));
    /* Paths outside graph/ and shared scene folders do not change anything. */
    assert(!rh_touch_path(&state, "data:/mesg/CN/CNzh/stage_parasol_00.msbt", &out));
    assert(!rh_touch_path(&state, "data:/graph/scene/common/info.spr", &out));
    open_path("data:/graph/menu/main_menu/main.spr");
    expect(SS_TOUCH_MENU, 0, 0);

    /* A-only minigame: both halves press A. */
    open_path("data:/graph/prologue/flydisc/type_00/prologue.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_A, 0);
    open_path("data:/graph/scene/common/pause.spr");
    open_path("data:/graph/scene/flydisc/type_00/main.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_A, 0);
    assert(!rh_touch_path(&state, "data:/graph/scene/flydisc/type_00/bg.spr", &out));
    open_path("data:/graph/menu/main_menu/main.spr");
    expect(SS_TOUCH_MENU, 0, 0);

    /* One direction, entered without a title card. */
    open_path("data:/graph/scene/parasol/type_00/main.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_DOWN, 0);
    open_path("data:/graph/result/result.spr");
    expect(SS_TOUCH_MENU, 0, 0);

    /* Left and right steps use all four directions. */
    open_path("data:/graph/prologue/dancer/type_00/prologue.spr");
    expect(SS_TOUCH_DPAD, SS_TOUCH_DIR_LEFT | SS_TOUCH_DIR_RIGHT, 0);

    /* B minigame. */
    open_path("data:/graph/prologue/foot_race/type_00/prologue.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_B, 0);

    /* A remix combines its scenes; a new title card starts over. */
    open_path("data:/graph/prologue/remix/type_00/prologue.spr");
    expect(SS_TOUCH_DPAD, 0, 0);
    open_path("data:/graph/scene/ring/type_00/main.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_A, 0);
    open_path("data:/graph/scene/hurdle/type_00/main.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_DOWN, 0);
    open_path("data:/graph/scene/wiper/type_00/main.spr");
    expect(SS_TOUCH_DPAD, SS_TOUCH_DIR_LEFT | SS_TOUCH_DIR_DOWN, 0);
    open_path("data:/graph/scene/rolling/type_00/main.spr");
    expect(SS_TOUCH_DPAD, SS_TOUCH_DIR_LEFT | SS_TOUCH_DIR_RIGHT | SS_TOUCH_DIR_DOWN, 0);
    open_path("data:/graph/prologue/wiper/type_00/prologue.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_LEFT, 0);

    /* Drum Lessons show the full controller. Modes with many buttons and unknown scenes keep the D-pad. */
    open_path("data:/graph/scene/drummer/type_00/main.spr");
    open_path("data:/graph/prologue/drummer/type_00/prologue.spr");
    expect(SS_TOUCH_PAD, 0, 0);
    open_path("data:/graph/scene/audio_tile/type_00/main.spr");
    open_path("data:/graph/prologue/audio_tile/type_00/prologue.spr");
    expect(SS_TOUCH_DPAD, 0, 0);
    open_path("data:/graph/prologue/new_game/type_00/prologue.spr");
    expect(SS_TOUCH_DPAD, 0, 0);
    open_path("data:/graph/menu/main_menu/main.spr");
    expect(SS_TOUCH_MENU, 0, 0);

    /* BeatSpell: the left area steps with B; the small D-pad, X and Y stay. */
    open_path("data:/graph/beatspell/common/common.spr");
    assert(hint.layout == SS_TOUCH_ONE && hint.first == SS_BUTTON_B && hint.second == 0);
    assert(hint.extra == (SS_TOUCH_SHOW_DPAD | SS_TOUCH_SHOW_X | SS_TOUCH_SHOW_Y));
    assert(!rh_touch_path(&state, "data:/graph/beatspell/bg/castle_00.spr", &out));
    open_path("data:/graph/menu/main_menu/main.spr");
    expect(SS_TOUCH_MENU, 0, 0);
    open_path("data:/graph/prologue/flydisc/type_00/prologue.spr");
    expect(SS_TOUCH_ONE, SS_BUTTON_A, 0);
    puts("test_touch: ok");
    return 0;
}
