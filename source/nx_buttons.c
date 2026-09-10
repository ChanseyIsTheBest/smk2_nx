/* nx_buttons.c -- the two button jobs nx_pointer.c does not own: the Android
 * BACK key, and a deliberate quit combo.
 *
 * nx_pointer.c owns the pad for pointer purposes (cursor, gyro toggle,
 * sensitivity) and does not expose raw button state. Rather than widen its API
 * -- it is meant to be a drop-in shared between ports -- this keeps a second
 * PadState of its own. That is safe: padUpdate() reads HID shared memory into
 * whichever PadState you hand it, so two independent readers see the same
 * frame's input and neither consumes it from the other.
 *
 * MIT license -- see LICENSE.
 */

#include <switch.h>
#include "nx_buttons.h"

static PadState s_pad;
static int      s_init;

void nxb_init(void) {
  if (s_init) return;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&s_pad);
  s_init = 1;
}

void nxb_update(void) {
  if (!s_init) nxb_init();
  padUpdate(&s_pad);
}

/* B is the Android BACK key.
 *
 * '+' is deliberately NOT wired to Back, even though that is the obvious
 * second choice: nx_pointer uses '+' to toggle the on-screen cursor, so
 * doubling it up would fire a back-press every time you brought the cursor up
 * -- which in Supermonkey's menus means backing out of the screen you were
 * about to point at. */
int back_edge_pressed(void)  { return (padGetButtonsDown(&s_pad) & HidNpadButton_B) ? 1 : 0; }
int back_edge_released(void) { return (padGetButtonsUp(&s_pad)   & HidNpadButton_B) ? 1 : 0; }

/* No quit combo. HOME suspends and the applet close exits cleanly, which is the
 * behaviour Switch users expect, and every button that could form a combo is
 * already spoken for by the pointer controls ('+' cursor, '-' gyro, L/R
 * recenter, d-pad sensitivity). A combo built on those would fire its other
 * meaning on the way in. The engine can still ask to quit via
 * MainActivity.quitApplication, which sets jni_quit_requested. */
int should_quit(void) { return 0; }
