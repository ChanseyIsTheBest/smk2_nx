/* nx_portrait.h -- fit a portrait game onto the Switch's landscape display.
 * Values match the `portrait` setting in config.txt. See nx_portrait.c.
 */
#ifndef __NX_PORTRAIT_H__
#define __NX_PORTRAIT_H__

/* These ARE the `portrait` values from config.txt -- deliberately, so the
 * number in the file and the number in the code can never drift apart. */
typedef enum {
  NXPT_FIT     = 0,   /* no rotation; upright, correct aspect, 16:9 pillar   */
  NXPT_ROT_CW  = 1,   /* rotate clockwise; right Joy-Con up.  DEFAULT.       */
  NXPT_ROT_CCW = 2,   /* rotate anticlockwise; left Joy-Con up               */
  NXPT_STRETCH = 3,   /* fill the screen, wrong aspect. Undocumented extra.  */
} NxptMode;

/* Pure configuration: no GL, no libnx. Safe before any context exists. */
void     nxpt_configure(int game_w, int game_h, int disp_w, int disp_h, NxptMode m);

NxptMode nxpt_mode(void);
int      nxpt_active(void);

/* What to hand nwindowSetDimensions / nwindowSetTransform. */
void     nxpt_window_size(int *w, int *h);
unsigned nxpt_window_transform(void);

/* FIT only: remap the engine's game-space viewport/scissor rect onto the
 * screen. Returns 0 in every other mode -- pass the rect through unchanged. */
int      nxpt_map_viewport(int x, int y, int w, int h,
                           int *ox, int *oy, int *ow, int *oh);

/* FIT only: screen pixel -> game pixel. Returns 0 for points in the pillars. */
int      nxpt_map_input(float sx, float sy, float *gx, float *gy);
int      nxpt_game_to_screen(float gx, float gy, float *sx, float *sy);
void     nxpt_clamp_to_game(float *sx, float *sy);

/* Rotated modes only: rotate an ABSOLUTE touch point (already in game space).
 * Never apply to the cursor -- it is drawn into the game's own buffer, so the
 * compositor rotates it for free. */
void     nxpt_map_touch(float *x, float *y);
void     nxpt_unmap_touch(float *x, float *y);

#endif
