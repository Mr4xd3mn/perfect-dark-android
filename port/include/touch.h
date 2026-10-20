#ifndef _IN_TOUCH_H
#define _IN_TOUCH_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __ANDROID__

#include <SDL.h>

/* Call once after the OpenGL ES context and window are created. */
void touchInit(void);

/* Returns 1 while a menu, pause screen or cutscene is showing. */
s32 touchIsInMenu(void);

/* Associate the overlay with the SDL window (for size queries). */
void touchSetWindow(SDL_Window *wnd);

/* Feed SDL touch events into the virtual controls. Returns 1 if consumed. */
s32 touchHandleEvent(const SDL_Event *event);

/* Movement stick output, range -1.0f .. 1.0f. */
void touchGetLeftStick(f32 *x, f32 *y);

/* Look stick output, range -1.0f .. 1.0f (unused; look is relative). */
void touchGetRightStick(f32 *x, f32 *y);

/* Accumulated relative look movement since the last call, in pixels. */
void touchGetLookDelta(f32 *dx, f32 *dy);

/* Currently held buttons, as a CONT_ bitmask. */
u32 touchGetButtons(void);

/* Buttons that went down since the last touchEndFrame(). */
u32 touchGetButtonsPressed(void);

/*
 * Returns 1 (and clears the flag) if a tap happened in a menu. The port's
 * input code uses this to press the game's accept button for one sample.
 */
s32 touchGetMenuAccept(void);

/*
 * Returns the buttons that were tapped since the last call, and clears them.
 * A tap is a press that was never confirmed as a hold, so it only lasts a
 * single input sample.
 */
u32 touchGetTapButton(void);

/* Call at the end of each frame to clear the "pressed" state. */
void touchEndFrame(void);

/* Render the on-screen touch overlay into the current GL context. */
void touchRenderOverlay(void);

/* Settings, all driven by the options menu. */
s32  touchGetEnabled(void);
void touchSetEnabled(s32 v);
s32  touchGetShowGamepad(void);
void touchSetShowGamepad(s32 v);
s32  touchGetShowZones(void);
void touchSetShowZones(s32 v);
s32  touchGetHaptics(void);
void touchSetHaptics(s32 v);
f32  touchGetOverlayOpacity(void);
void touchSetOverlayOpacity(f32 v);
f32  touchGetStickSize(void);
void touchSetStickSize(f32 v);
f32  touchGetStickDeadzone(void);
void touchSetStickDeadzone(f32 v);
f32  touchGetLookSensX(void);
void touchSetLookSensX(f32 v);
f32  touchGetLookSensY(void);
void touchSetLookSensY(f32 v);

/* Master look sensitivity; multiplies both axes. */
f32  touchGetAimSensitivity(void);
void touchSetAimSensitivity(f32 v);

/* Overall size multiplier for every button. */
f32  touchGetButtonScale(void);
void touchSetButtonScale(f32 v);

/* Layout editor: when on, the buttons can be dragged around and are not
   passed to the game. */
s32  touchGetEditMode(void);
void touchSetEditMode(s32 v);

/* Puts every button back where it started. */
void touchResetLayout(void);
s32  touchGetLayout(void);
void touchSetLayout(s32 v);

/* Overlay layout presets. */
#define TOUCH_LAYOUT_GAMEPAD 0
#define TOUCH_LAYOUT_ARC      1
#define TOUCH_LAYOUT_MINIMAL  2

#endif /* __ANDROID__ */

#ifdef __cplusplus
}
#endif

#endif /* _IN_TOUCH_H */
