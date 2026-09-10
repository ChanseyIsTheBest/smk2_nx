/* nx_buttons.h -- BACK key + quit combo. See nx_buttons.c. */
#ifndef __NX_BUTTONS_H__
#define __NX_BUTTONS_H__

void nxb_init(void);           /* called from platform.c's init            */
void nxb_update(void);         /* once per frame, from padUpdate_all()     */

int  back_edge_pressed(void);  /* B pressed this frame  -> Android Back    */
int  back_edge_released(void); /* B released this frame                    */
int  should_quit(void);        /* always 0: HOME exits. See nx_buttons.c   */

#endif
