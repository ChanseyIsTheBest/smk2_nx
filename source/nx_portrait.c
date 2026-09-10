/* nx_portrait.c -- fit a PORTRAIT game onto the Switch's LANDSCAPE display.
 *
 * Bloons Supermonkey renders 1080x1920 (9:16). The panel is landscape.
 *
 * DEFAULT: ROT_CW -- rotated clockwise to fill the screen, right Joy-Con up.
 *
 * `portrait 0` selects FIT instead: upright, correct aspect, 16:9 pillar. That
 * is the mode confirmed working on hardware, and it is the one to fall back to
 * if a rotation ever misbehaves. In FIT the native window stays at the panel's
 * own resolution and the ENGINE'S OWN
 * viewport and scissor rectangles are scaled into a centred destination rect:
 * for 1080x1920 into 1920x1080 that is 608x1080 at x=656. GL then scales all
 * the geometry to match, for free. No offscreen buffer, no shader, no
 * compositor tricks.
 *
 * It is safe to remap the engine's viewport like this specifically because this
 * engine never renders to its own FBOs -- every frame in the boot log reports
 * fbo=0. glViewport_fake skips the remap whenever a non-zero FBO is bound, in
 * case a future build changes that.
 *
 * THE OTHER MODES, and what they cost:
 *
 *   STRETCH  window set to the game's size with no transform, so the compositor
 *            squeezes 1080x1920 into a landscape layer. Fills the screen; wrong
 *            aspect. Only sensible if you actively prefer it to black bars.
 *
 *   ROT_CW / ROT_CCW
 *            window set to the game's size WITH a compositor rotation, so the
 *            game runs full-screen at native resolution -- but you have to turn
 *            the console. Practical handheld with the Joy-Cons off; useless on
 *            a TV.
 *
 * WHAT WAS TRIED AND ABANDONED
 * The first implementation rendered into an offscreen FBO and blitted it with a
 * shader. On hardware it failed at the first hurdle: glCheckFramebufferStatus
 * returned 0, which is not an incompleteness code but "the call itself
 * errored". Presentation was disabled, the engine drew straight to the screen
 * -- glViewport(0,0,1080,1920) into a 1920x1080 framebuffer, which GL clipped
 * to the left 1080x1080 -- and the result was a square of game in the corner of
 * a black screen. The FBO path is gone; nothing here needs it any more.
 *
 * INPUT
 *   FIT      nx_pointer works in DISPLAY space and events are mapped back to
 *            game space, with pillar hits rejected.
 *   ROT_*    nx_pointer works in GAME space; the cursor needs no correction
 *            (relative input, drawn into the game's own buffer), but absolute
 *            touches are rotated.
 *   STRETCH  nothing to do.
 *
 * MIT license -- see LICENSE.
 */

#include <stdio.h>
#include <string.h>

#include "nx_portrait.h"
#include "util.h"

/* libnx NativeWindowTransform values, under local names. They are a stable part
 * of the Android native-window ABI the Switch inherited, and spelling them out
 * keeps this file buildable in the host audit where there is no libnx. */
#define XFORM_NONE    0x00
#define XFORM_ROT_90  0x04
#define XFORM_ROT_270 0x07

static int      g_game_w = 1080, g_game_h = 1920;
static int      g_disp_w = 1920, g_disp_h = 1080;
static NxptMode g_mode   = NXPT_ROT_CW;
static int      g_active;

/* Destination rectangle of the game image inside the display, in pixels.
 * Computed once and shared by the viewport remap and the inverse input
 * mapping, so the two can never disagree. FIT only. */
static float g_dst_x, g_dst_y, g_dst_w, g_dst_h;

static void compute_dst(void) {
  const float ga = (float)g_game_w / (float)g_game_h;
  const float da = (float)g_disp_w / (float)g_disp_h;
  if (ga > da) { g_dst_w = (float)g_disp_w; g_dst_h = g_dst_w / ga; }
  else         { g_dst_h = (float)g_disp_h; g_dst_w = g_dst_h * ga; }
  g_dst_x = ((float)g_disp_w - g_dst_w) * 0.5f;
  g_dst_y = ((float)g_disp_h - g_dst_h) * 0.5f;
}

void nxpt_configure(int game_w, int game_h, int disp_w, int disp_h, NxptMode m) {
  if (game_w > 0) g_game_w = game_w;
  if (game_h > 0) g_game_h = game_h;
  if (disp_w > 0) g_disp_w = disp_w;
  if (disp_h > 0) g_disp_h = disp_h;
  if ((int)m < 0 || (int)m > 3) m = NXPT_ROT_CW;
  g_mode   = m;
  g_active = 1;
  compute_dst();

  if (m == NXPT_FIT)
    debugPrintf("nxpt: portrait 0 (no rotation, 16:9 pillar) -- game %dx%d -> %.0f,%.0f %.0fx%.0f of %dx%d\n",
                g_game_w, g_game_h, g_dst_x, g_dst_y, g_dst_w, g_dst_h, g_disp_w, g_disp_h);
  else
    debugPrintf("nxpt: portrait %d (%s) -- window %dx%d, transform 0x%02x\n",
                (int)m,
                m == NXPT_STRETCH ? "stretched 16:9" :
                m == NXPT_ROT_CW  ? "rotated clockwise, right Joy-Con up"
                                  : "rotated anticlockwise, left Joy-Con up",
                g_game_w, g_game_h, nxpt_window_transform());
}

NxptMode nxpt_mode(void)   { return g_mode; }
int      nxpt_active(void) { return g_active; }

/* FIT keeps the window at the panel's resolution and scales inside it. Every
 * other mode hands the compositor a buffer of the game's own size. */
void nxpt_window_size(int *w, int *h) {
  if (g_mode == NXPT_FIT) { *w = g_disp_w; *h = g_disp_h; }
  else                    { *w = g_game_w; *h = g_game_h; }
}

unsigned nxpt_window_transform(void) {
  switch (g_mode) {
    case NXPT_ROT_CW:  return XFORM_ROT_90;
    case NXPT_ROT_CCW: return XFORM_ROT_270;
    default:           return XFORM_NONE;
  }
}

int nxpt_map_viewport(int x, int y, int w, int h,
                      int *ox, int *oy, int *ow, int *oh) {
  if (!g_active || g_mode != NXPT_FIT) return 0;
  const float sx = g_dst_w / (float)g_game_w;
  const float sy = g_dst_h / (float)g_game_h;
  *ox = (int)(g_dst_x + x * sx + 0.5f);
  *oy = (int)(g_dst_y + y * sy + 0.5f);
  *ow = (int)(w * sx + 0.5f);
  *oh = (int)(h * sy + 0.5f);
  return 1;
}

/* Screen pixel -> game pixel. Returns 0 for points in the black pillars, which
 * the caller should drop: clamping them would turn the pillars into a sticky
 * border you can drag along. FIT only. */
int nxpt_map_input(float sx, float sy, float *gx, float *gy) {
  if (!g_active || g_mode != NXPT_FIT) { *gx = sx; *gy = sy; return 1; }
  const float u = (sx - g_dst_x) / g_dst_w;
  const float v = (sy - g_dst_y) / g_dst_h;
  if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return 0;
  *gx = u * (float)g_game_w;
  *gy = v * (float)g_game_h;
  return 1;
}

/* Exact inverse of nxpt_map_input, defined next to it because the two must
 * agree and a round-trip test is the cheapest way to keep them agreeing. */
int nxpt_game_to_screen(float gx, float gy, float *sx, float *sy) {
  if (!g_active || g_mode != NXPT_FIT) { *sx = gx; *sy = gy; return 1; }
  if (gx < 0.0f || gx > (float)g_game_w || gy < 0.0f || gy > (float)g_game_h) return 0;
  *sx = g_dst_x + (gx / (float)g_game_w) * g_dst_w;
  *sy = g_dst_y + (gy / (float)g_game_h) * g_dst_h;
  return 1;
}

/* Clamp a screen point into the game rectangle, for drag events that wander
 * into the pillars: dropping those would leave the engine holding a touch that
 * never ends. */
void nxpt_clamp_to_game(float *sx, float *sy) {
  if (!g_active || g_mode != NXPT_FIT) return;
  const float x0 = g_dst_x, x1 = g_dst_x + g_dst_w - 1.0f;
  const float y0 = g_dst_y, y1 = g_dst_y + g_dst_h - 1.0f;
  if (*sx < x0) *sx = x0;
  if (*sx > x1) *sx = x1;
  if (*sy < y0) *sy = y0;
  if (*sy > y1) *sy = y1;
}

/* Rotated modes: the incoming point is already in game space, because
 * nx_pointer scaled the raw panel reading by game/panel. Normalising by the
 * game size therefore recovers where on the PANEL the finger was, which is what
 * the rotation has to act on.
 *
 * Clockwise: the buffer's top edge ends up on the screen's right, so a finger
 * at the panel's top-left is touching the buffer's bottom-left. Anticlockwise
 * is the mirror of that. */
void nxpt_map_touch(float *x, float *y) {
  if (!g_active || (g_mode != NXPT_ROT_CW && g_mode != NXPT_ROT_CCW)) return;
  const float u = *x / (float)g_game_w;
  const float v = *y / (float)g_game_h;
  if (g_mode == NXPT_ROT_CW) { *x = v * g_game_w;          *y = (1.0f - u) * g_game_h; }
  else                       { *x = (1.0f - v) * g_game_w; *y = u * g_game_h; }
}

void nxpt_unmap_touch(float *x, float *y) {
  if (!g_active || (g_mode != NXPT_ROT_CW && g_mode != NXPT_ROT_CCW)) return;
  const float u = *x / (float)g_game_w;
  const float v = *y / (float)g_game_h;
  if (g_mode == NXPT_ROT_CW) { *x = (1.0f - v) * g_game_w; *y = u * g_game_h; }
  else                       { *x = v * g_game_w;          *y = (1.0f - u) * g_game_h; }
}
