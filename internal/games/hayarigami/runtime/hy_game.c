/* Hayarigami uses Touchable's shared services. No fixed-address changes yet. */
#include "dm.h"
#include "touchable_game.h"

void touchable_game_install(void) {
    LOG("Hayarigami adapter: shared native runtime; no fixed-address patches");
}

int touchable_game_host_call(int number, uint64_t *registers) {
    (void)number;
    (void)registers;
    return 0;
}
