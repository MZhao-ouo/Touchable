# Hayarigami screen touch and unobstructed controls

Goal: Forward direct screen input to the original game. Put opaque controls outside the complete 16:9 picture.

Scope: Hayarigami opts into the native layout. Other games keep their current layouts. Preserve app identity and saves.

1. Add a failing native HID test. Check ABI sizes, short taps, start/end, movement, cancellation, bounded capacity and concurrent access. Implement a locked single-finger bridge. Keep the existing empty state when no native touch exists.
2. Add a failing geometry test. Fit the game between two rails of seven 44-point controls. Use two bottom rows on short windows. Check safe areas, no overlaps, all fourteen controls and coordinate boundaries. Implement the shared pure C geometry.
3. Add a native UIKit screen surface and opaque button rails. Use the same game rectangle for display and touch mapping. Release inputs on focus or layout change. Opt in only for Hayarigami and increment to Build 8.
4. Run native tests and the Python suite. Generate and compile the signed iOS app. Install as an update if the device is available. Verify native menu touch on the device, or state the missing device evidence. Commit and push source-only changes to the existing PR.

Review focus: Fast down/up between polls must be seen. Touches in controls must not reach the screen. Layout changes must release held input. Every control must remain outside the picture and within safe bounds. All capacity variants must preserve adjacent guest memory.
