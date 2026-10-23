/*
 * On-screen touch controls for the Perfect Dark Android port.
 *
 * Provides:
 *   - a floating analog stick for movement (left thumb)
 *   - a relative drag area for camera look (right thumb), fed into the port's
 *     existing mouselook code
 *   - a virtual gamepad overlay with labelled buttons
 *
 * The touch state is exposed to the port through the same OSContPad fields a
 * physical controller would fill, so the game's own control code is used
 * unchanged. See port/src/input.c for the wiring.
 */

#ifdef __ANDROID__
/* The N64 headers have to come before <SDL.h>: they redeclare bcopy/bcmp,
   which the C library already declares with the "overloadable" attribute. */
#include <ultra64.h>
#include "constants.h"
#include "data.h"
#include "bss.h"
#endif

#include <SDL.h>
#include <math.h>
#include <string.h>

#ifdef __ANDROID__
#include <GLES2/gl2.h>
#include <PR/os_thread.h>
#include <PR/os_cont.h>
#endif

#include "platform.h"
#include "system.h"
#include "touch.h"

#ifdef __ANDROID__
#include "config.h"
#include "input.h"
#include "utils.h"

extern s32 g_StageNum;

/*
 * True whenever the game is showing something that is not the game itself:
 * a menu stage, the in-game pause menu, a cutscene, or the end screen.
 * The on-screen controls are hidden then, so they do not cover the UI.
 */
s32 touchIsInMenu(void)
{
	/* Menu stages: title screen, boot pak menu, credits, 4MB menu. */
	if (g_StageNum >= STAGE_TITLE) {
		return 1;
	}

	/*
	 * The in-game menu, including the pause menu. Every player's menu is
	 * checked, because the menu that is open is not necessarily the one
	 * belonging to the current player index.
	 */
	for (s32 i = 0; i < MAX_PLAYERS; ++i) {
		if (g_Menus[i].curdialog) {
			return 1;
		}
	}

	/* Paused. */
	if (g_Vars.currentplayer && g_Vars.currentplayer->pausemode != PAUSEMODE_UNPAUSED) {
		return 1;
	}

	/* Cutscenes. */
	if (g_Vars.in_cutscene) {
		return 1;
	}

	return 0;
}
#endif

#ifdef __ANDROID__

#define TOUCH_MAX_POINTS 10

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

/* Button ids. The order matters: it is the order they are drawn in. */
enum {
	BTN_START = 0,
	BTN_R,    // aim mode
	BTN_B,    // weapon switch
	BTN_A,    // action / open
	BTN_Z,    // fire
	BTN_L,    // alt fire (L trigger)
	BTN_MENU_UP,
	BTN_MENU_DOWN,
	BTN_MENU_LEFT,
	BTN_MENU_RIGHT,
	BTN_MENU_OK,   // menu: confirm
	BTN_MENU_BACK, // menu: back
	BTN_RELOAD,    // append: preserve saved Touch.BtnN positions from older versions
	BTN_COUNT
};

typedef struct {
	u32 contBit;   // N64 controller bit driven by this button
	const char *label;
	f32 r, g, b;   // colour
	/*
	 * Position relative to a screen corner, in "units". A unit is a fixed
	 * fraction of the screen's smaller dimension, so the layout keeps its
	 * proportions on any aspect ratio instead of being stretched. The
	 * offsets already include the button's radius, so nothing is clipped.
	 */
	f32 ox, oy;    // offset from the corner
	f32 radius;    // radius, in units
	s32 anchor;    // which corner the offset is measured from
	s32 shoulder;  // drawn as a wide bar instead of a round button
	s32 inGame;    // shown during gameplay
	s32 inMenu;    // shown while a menu is open
} TouchButton;

enum {
	ANCHOR_BR = 0,   // bottom right
	ANCHOR_TR,       // top right
	ANCHOR_TL,       // top left
	ANCHOR_BL,       // bottom left
};

#define TOUCH_UNIT (0.085f)  // one layout unit, relative to the smaller dimension

/*
 * The look speed is scaled relative to a mouse, so that a swipe of the same
 * distance turns the camera about as much as moving a mouse would. Without
 * this the touch look feels noticeably slower than the port's mouselook.
 *
 * This is a base factor rather than part of the default setting, so that it
 * also applies to an existing configuration file.
 */
#define TOUCH_LOOK_BASE_SENS 2.0f

/*
 * The button cluster is laid out like an N64 pad: the fire button in the
 * bottom corner with the action buttons fanned out above it.
 *
 * The menu buttons use the C-buttons, which is what the game's menus read
 * for directional navigation.
 */
static const TouchButton g_Btns[BTN_COUNT] = {
	/* contBit     label    r      g      b      ox      oy     rad  anchor     shoulder inGame inMenu */
	{ CONT_START,  "START", 0.78f, 0.82f, 0.90f, -1.25f, -1.25f, 0.85f, ANCHOR_TR, 0, 1, 0 },
	{ CONT_R,     "R",     0.95f, 0.74f, 0.22f, -3.10f, -5.10f, 0.95f, ANCHOR_BR, 0, 1, 0 },
	{ CONT_B,     "B",     0.30f, 0.60f, 0.95f, -5.10f, -3.10f, 0.95f, ANCHOR_BR, 0, 1, 0 },
	{ CONT_A,     "A",     0.30f, 0.85f, 0.45f, -3.10f, -3.10f, 0.95f, ANCHOR_BR, 0, 1, 0 },
	{ CONT_G,     "Z",     0.95f, 0.30f, 0.30f, -1.75f, -1.75f, 1.25f, ANCHOR_BR, 0, 1, 0 },
	{ CONT_L,     "L",     0.95f, 0.85f, 0.35f, -1.45f, -1.25f, 0.85f, ANCHOR_TL, 1, 1, 0 },
	{ CONT_E,     "",      0.80f, 0.84f, 0.92f, -2.05f, -2.95f, 0.80f, ANCHOR_BL, 0, 0, 1 },
	{ CONT_D,     "",      0.80f, 0.84f, 0.92f, -2.05f, -1.15f, 0.80f, ANCHOR_BL, 0, 0, 1 },
	{ CONT_C,     "",      0.80f, 0.84f, 0.92f, -1.15f, -2.05f, 0.80f, ANCHOR_BL, 0, 0, 1 },
	{ CONT_F,     "",      0.80f, 0.84f, 0.92f, -2.95f, -2.05f, 0.80f, ANCHOR_BL, 0, 0, 1 },
	{ CONT_A,     "OK",    0.30f, 0.85f, 0.45f, -1.45f, -1.45f, 1.00f, ANCHOR_BR, 0, 0, 1 },
	{ CONT_B,     "BACK",  0.95f, 0.35f, 0.35f, -4.05f, -1.45f, 0.90f, ANCHOR_BR, 0, 0, 1 },
	{ BUTTON_RELOAD, "RELOAD", 0.70f, 0.82f, 0.95f, -5.35f, -5.35f, 0.95f, ANCHOR_BR, 0, 1, 0 },
};

typedef struct {
	/*
	 * The identity of a pointer. SDL reports a touch device id and a finger
	 * id; the pair together is the only reliable way to tell two fingers
	 * apart, because the finger id is only unique per device.
	 */
	SDL_TouchID touchId;
	SDL_FingerID fingerId;

	f32 x, y;            // current position, in pixels
	f32 originX, originY; // where the finger went down, in pixels
	f32 lookDX, lookDY;   // look movement accumulated by this pointer

	s32 active;
	s32 zone;            // ZONE_* below
	s32 button;        // button id once the press has been confirmed
	s32 pendingButton; // button id that has not been confirmed yet
	u32 downFrame;     // frame the finger went down on
} TouchPoint;

enum {
	ZONE_NONE = -1,
	ZONE_MOVE = 0,   // left thumb: floating movement stick
	ZONE_LOOK = 1,   // right thumb: relative camera look
	ZONE_BUTTON = 2,
};

static TouchPoint g_Touches[TOUCH_MAX_POINTS];

/* Movement stick output, -1 .. 1 */
static f32 g_StickX = 0.f, g_StickY = 0.f;

/* Currently held buttons, as a CONT_ bitmask */
static u32 g_Buttons = 0;

/* Set when a button went down since the last frame, for haptics. */
static u32 g_ButtonsPressed = 0;

/*
 * A button press is only confirmed once the finger has stayed still for a
 * couple of frames. Until then it is "pending": if the finger moves, the
 * press is dropped and the gesture becomes camera movement instead. That is
 * what stops a drag that happens to start on the fire button from shooting.
 */
static u32 g_Frame = 0;
#define TOUCH_PRESS_DELAY 2

/* One-shot button presses, from taps. Consumed by the input layer. */
static u32 g_TapButton = 0;

/* Set when a tap happened in a menu; consumed by the next input sample. */
static s32 g_MenuAccept = 0;

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

static s32 g_TouchEnabled = 1;
static s32 g_ShowGamepad = 1;
static s32 g_ShowZones = 0;
static s32 g_Haptics = 1;
static f32 g_OverlayOpacity = 0.75f;
static f32 g_StickSize = 1.0f;      // multiplier on the stick radius
static f32 g_StickDeadzone = 0.15f;
static f32 g_LookSensX = 1.0f;
static f32 g_LookSensY = 1.0f;
static f32 g_AimSensitivity = 1.5f; // master multiplier on both axes
static f32 g_ButtonScale = 1.0f;     // multiplier on every button's size

/*
 * Per button position offsets, as a fraction of the screen. These let the
 * player move the virtual buttons around to suit their hands.
 */
static f32 g_BtnOffX[BTN_COUNT];
static f32 g_BtnOffY[BTN_COUNT];

/* Layout editor mode: the buttons can be dragged around. */
static s32 g_EditMode = 0;

#define TOUCH_LAYOUT_GAMEPAD 0
#define TOUCH_LAYOUT_ARC      1
#define TOUCH_LAYOUT_MINIMAL  2
static s32 g_Layout = TOUCH_LAYOUT_GAMEPAD;

s32 touchGetEnabled(void)        { return g_TouchEnabled; }
void touchSetEnabled(s32 v)      { g_TouchEnabled = !!v; }
s32 touchGetShowGamepad(void)    { return g_ShowGamepad; }
void touchSetShowGamepad(s32 v)  { g_ShowGamepad = !!v; }
s32 touchGetShowZones(void)      { return g_ShowZones; }
void touchSetShowZones(s32 v)    { g_ShowZones = !!v; }
s32 touchGetHaptics(void)        { return g_Haptics; }
void touchSetHaptics(s32 v)      { g_Haptics = !!v; }
f32 touchGetOverlayOpacity(void) { return g_OverlayOpacity; }
void touchSetOverlayOpacity(f32 v) { g_OverlayOpacity = v; }
f32 touchGetStickSize(void)      { return g_StickSize; }
void touchSetStickSize(f32 v)    { g_StickSize = v; }
f32 touchGetStickDeadzone(void)  { return g_StickDeadzone; }
void touchSetStickDeadzone(f32 v){ g_StickDeadzone = v; }
f32 touchGetLookSensX(void)      { return g_LookSensX; }
void touchSetLookSensX(f32 v)    { g_LookSensX = v; }
f32 touchGetLookSensY(void)      { return g_LookSensY; }
void touchSetLookSensY(f32 v)    { g_LookSensY = v; }
f32 touchGetAimSensitivity(void) { return g_AimSensitivity; }
void touchSetAimSensitivity(f32 v) { g_AimSensitivity = v; }
f32 touchGetButtonScale(void)    { return g_ButtonScale; }
void touchSetButtonScale(f32 v)  { g_ButtonScale = v; }
s32 touchGetLayout(void)         { return g_Layout; }
void touchSetLayout(s32 v)       { g_Layout = v; }

s32 touchGetEditMode(void)       { return g_EditMode; }
void touchSetEditMode(s32 v)     { g_EditMode = !!v; }

void touchResetLayout(void)
{
	for (s32 i = 0; i < BTN_COUNT; ++i) {
		g_BtnOffX[i] = 0.f;
		g_BtnOffY[i] = 0.f;
	}
}

/* Persist the settings in the config file. */
PD_CONSTRUCTOR static void touchConfigInit(void)
{
	configRegisterInt("Touch.Enabled", &g_TouchEnabled, 0, 1);
	configRegisterInt("Touch.ShowGamepad", &g_ShowGamepad, 0, 1);
	configRegisterInt("Touch.ShowZones", &g_ShowZones, 0, 1);
	configRegisterInt("Touch.Haptics", &g_Haptics, 0, 1);
	configRegisterInt("Touch.Layout", &g_Layout, 0, 2);
	configRegisterFloat("Touch.Opacity", &g_OverlayOpacity, 0.1f, 1.f);
	configRegisterFloat("Touch.StickSize", &g_StickSize, 0.5f, 2.f);
	configRegisterFloat("Touch.Deadzone", &g_StickDeadzone, 0.f, 0.5f);
	configRegisterFloat("Touch.LookSensX", &g_LookSensX, 0.1f, 6.f);
	configRegisterFloat("Touch.LookSensY", &g_LookSensY, 0.1f, 6.f);
	configRegisterFloat("Touch.AimSensitivity", &g_AimSensitivity, 0.1f, 6.f);
	configRegisterFloat("Touch.ButtonScale", &g_ButtonScale, 0.6f, 1.6f);

	/* The position of each button, so the layout can be rearranged. */
	for (s32 i = 0; i < BTN_COUNT; ++i) {
		configRegisterFloat(strFmt("Touch.Btn%d.X", (int)i), &g_BtnOffX[i], -1.f, 1.f);
		configRegisterFloat(strFmt("Touch.Btn%d.Y", (int)i), &g_BtnOffY[i], -1.f, 1.f);
	}
}

/* ------------------------------------------------------------------ */
/* GL state                                                            */
/* ------------------------------------------------------------------ */

static GLuint g_Shader = 0;
static GLint g_LocProj = -1;
static GLint g_LocPos = -1;
static GLint g_LocColor = -1;
static GLint g_LocUV = -1;
static GLint g_LocTex = -1;

/*
 * The overlay needs its own vertex array object and buffer. The game keeps
 * its attributes enabled in its own VAO, and touching those would make the
 * whole game render black.
 */
static GLuint g_Vbo = 0;
static GLuint g_Vao = 0;
static GLuint g_Tex = 0;

/* Vertex array objects are core in GLES3 but an extension in GLES2, so they
   are resolved at runtime instead of being linked against directly. */
typedef void (*PFN_touchGenVertexArrays)(GLsizei, GLuint *);
typedef void (*PFN_touchBindVertexArray)(GLuint);
static PFN_touchGenVertexArrays pfnGenVertexArrays = NULL;
static PFN_touchBindVertexArray pfnBindVertexArray = NULL;
#define GL_VERTEX_ARRAY_BINDING_OES 0x85B5

static SDL_Window *g_TouchWindow = NULL;

static s32 g_Initialized = 0;

/* ------------------------------------------------------------------ */
/* Layout helpers                                                      */
/* ------------------------------------------------------------------ */

static s32 g_ScreenW = 0, g_ScreenH = 0;

/*
 * The size of the framebuffer the overlay is actually drawn into.
 *
 * SDL reports the size of the surface, but that is not always the size of
 * the framebuffer: some drivers hand out a smaller framebuffer than the
 * surface has and clamp the viewport to it. Adreno on Snapdragon does this.
 * The overlay has to be laid out in the framebuffer's size, because that is
 * what ends up on the screen - otherwise the buttons are drawn in one place
 * and the touches are read in another.
 */
static s32 g_FbW = 0, g_FbH = 0;

/* Set once the sizes have been written to the log. */
static s32 g_LoggedSizes = 0;

/* Button positions in pixels, recalculated whenever the layout changes. */
static f32 g_BtnPixelX[BTN_COUNT];
static f32 g_BtnPixelY[BTN_COUNT];
static f32 g_BtnPixelR[BTN_COUNT];

/*
 * The drawable size is the real on-screen size in physical pixels. On Android
 * the window is created with a configured size but the surface is whatever the
 * display gives us, so a stale size would put the overlay in the wrong place.
 */
static void touchGetSurfaceSize(s32 *w, s32 *h)
{
	*w = 0;
	*h = 0;
	if (!g_TouchWindow) {
		return;
	}
	SDL_GL_GetDrawableSize(g_TouchWindow, w, h);
	if (*w <= 0 || *h <= 0) {
		SDL_GetWindowSize(g_TouchWindow, w, h);
	}
}

/*
 * Works out the real size of the framebuffer.
 *
 * The viewport is set to the full surface and then read back: a driver that
 * clamps it to a smaller framebuffer reports the size it actually gave us.
 * The old viewport is put back, so this does not disturb the game.
 */
static void touchUpdateFramebufferSize(s32 surfW, s32 surfH)
{
	if (surfW <= 0 || surfH <= 0) {
		return;
	}

	GLint vp[4] = { 0, 0, 0, 0 };

	glGetIntegerv(GL_VIEWPORT, vp);
	glViewport(0, 0, surfW, surfH);
	glGetIntegerv(GL_VIEWPORT, vp);
	glViewport(vp[0], vp[1], vp[2], vp[3]);

	if (vp[2] > 0 && vp[3] > 0) {
		g_FbW = vp[2];
		g_FbH = vp[3];
	}
}

/*
 * The size the layout is worked out in: the framebuffer if we know it, and
 * the surface otherwise.
 */
static void touchGetLayoutSize(s32 *w, s32 *h)
{
	s32 surfW = 0, surfH = 0;

	touchGetSurfaceSize(&surfW, &surfH);
	if (surfW <= 0 || surfH <= 0) {
		*w = 0;
		*h = 0;
		return;
	}

	touchUpdateFramebufferSize(surfW, surfH);

	if (g_FbW > 0 && g_FbH > 0) {
		*w = g_FbW;
		*h = g_FbH;
	} else {
		*w = surfW;
		*h = surfH;
	}
}

static inline f32 touchMinDim(void)
{
	return (g_ScreenW < g_ScreenH) ? (f32)g_ScreenW : (f32)g_ScreenH;
}

/*
 * The position and size of a button for the current layout.
 *
 * Everything is measured in "units", where a unit is a fixed fraction of the
 * screen's smaller dimension. That keeps the buttons perfectly round and
 * keeps the layout proportional on any aspect ratio, instead of stretching
 * it to fill the screen.
 *
 * The offsets are relative to an anchor corner, and are negative, so the
 * buttons sit inside the screen with a margin.
 */
static void touchButtonLayout(s32 i, f32 *x, f32 *y, f32 *radius)
{
	const TouchButton *b = &g_Btns[i];
	const f32 unit = touchMinDim() * TOUCH_UNIT;

	f32 ox = b->ox;
	f32 oy = b->oy;
	f32 rad = b->radius;

	switch (g_Layout) {
	case TOUCH_LAYOUT_ARC:
		/* Fan the action buttons out along an arc in the corner. */
		if (i == BTN_Z) {
			ox = -1.45f; oy = -1.45f; rad = 1.25f;
		} else if (i == BTN_A) {
			ox = -3.15f; oy = -2.55f; rad = 0.85f;
		} else if (i == BTN_B) {
			ox = -5.05f; oy = -2.55f; rad = 0.85f;
		} else if (i == BTN_R) {
			ox = -3.15f; oy = -4.55f; rad = 0.85f;
		}
		break;
	case TOUCH_LAYOUT_MINIMAL:
		if (i == BTN_Z) {
			ox = -1.55f; oy = -1.55f; rad = 1.35f;
		} else if (i == BTN_A) {
			ox = -3.25f; oy = -2.35f; rad = 0.90f;
		} else if (i == BTN_B) {
			ox = -5.25f; oy = -2.35f; rad = 0.90f;
		} else if (i == BTN_R) {
			ox = -3.25f; oy = -4.45f; rad = 0.90f;
		}
		break;
	case TOUCH_LAYOUT_GAMEPAD:
	default:
		break;
	}

	switch (b->anchor) {
	case ANCHOR_TR:
		*x = (f32)g_ScreenW + ox * unit;
		*y = -oy * unit;
		break;
	case ANCHOR_TL:
		*x = -ox * unit;
		*y = -oy * unit;
		break;
	case ANCHOR_BL:
		*x = -ox * unit;
		*y = (f32)g_ScreenH + oy * unit;
		break;
	case ANCHOR_BR:
	default:
		*x = (f32)g_ScreenW + ox * unit;
		*y = (f32)g_ScreenH + oy * unit;
		break;
	}

	/* The player can move each button, and scale them all. */
	*x += g_BtnOffX[i] * (f32)g_ScreenW;
	*y += g_BtnOffY[i] * (f32)g_ScreenH;

	*radius = rad * unit * g_ButtonScale;
}

static void touchLayoutButtons(void)
{
	for (s32 i = 0; i < BTN_COUNT; ++i) {
		touchButtonLayout(i, &g_BtnPixelX[i], &g_BtnPixelY[i], &g_BtnPixelR[i]);
	}
}

/*
 * Returns 1 if the button is part of the current layout: the gamepad while
 * playing, the confirm/back pair while a menu is open.
 */
static s32 touchButtonVisible(s32 btn)
{
	if (btn < 0 || btn >= BTN_COUNT) {
		return 0;
	}
	if (g_EditMode) {
		/* The editor shows every button, so they can all be moved. */
		return 1;
	}
	return touchIsInMenu() ? g_Btns[btn].inMenu : g_Btns[btn].inGame;
}

/*
 * The order the buttons are tested in. The closest one wins anyway, but the
 * fire button is listed first so that it takes precedence in a tie.
 */
static const s32 g_BtnOrder[BTN_COUNT] = {
	BTN_Z, BTN_A, BTN_B, BTN_R, BTN_START, BTN_L, BTN_RELOAD,
	BTN_MENU_OK, BTN_MENU_BACK,
	BTN_MENU_UP, BTN_MENU_DOWN, BTN_MENU_LEFT, BTN_MENU_RIGHT
};

/*
 * Returns the button under the given point, or -1.
 *
 * When several hit areas overlap - the direction pad buttons in particular -
 * the closest one wins, so a press always goes to the button the player was
 * actually aiming at.
 */
static s32 touchButtonAt(f32 x, f32 y)
{
	s32 best = -1;
	f32 bestScore = 0.f;

	for (s32 k = 0; k < BTN_COUNT; ++k) {
		const s32 i = g_BtnOrder[k];
		if (!touchButtonVisible(i)) {
			continue;
		}
		const TouchButton *b = &g_Btns[i];
		const f32 r = g_BtnPixelR[i];
		const f32 dx = x - g_BtnPixelX[i];
		const f32 dy = y - g_BtnPixelY[i];

		f32 score;
		if (b->shoulder) {
			/* A wide, short bar. */
			const f32 hw = r * 1.7f;
			const f32 hh = r * 0.8f;
			if (fabsf(dx) > hw || fabsf(dy) > hh) {
				continue;
			}
			score = fmaxf(fabsf(dx) / hw, fabsf(dy) / hh);
		} else {
			/*
			 * The direction pad buttons sit close together, so they get a
			 * tighter hit area to keep them from overlapping. The round
			 * buttons are bigger targets and get a generous one.
			 */
			const f32 factor = (i >= BTN_MENU_UP && i <= BTN_MENU_RIGHT) ? 1.05f : 1.25f;
			const f32 hit = r * factor;
			const f32 dist = sqrtf(dx * dx + dy * dy);
			if (dist > hit) {
				continue;
			}
			score = dist / hit;
		}

		if (best < 0 || score < bestScore) {
			best = i;
			bestScore = score;
		}
	}
	return best;
}

/* ------------------------------------------------------------------ */
/* Touch handling                                                      */
/* ------------------------------------------------------------------ */

/*
 * Finds the pointer a touch event belongs to. The touch device id and the
 * finger id together identify a pointer; using the finger id alone would
 * confuse two fingers from different touch devices.
 */
static inline s32 touchFindPoint(SDL_TouchID touchId, SDL_FingerID fingerId)
{
	for (s32 i = 0; i < TOUCH_MAX_POINTS; ++i) {
		if (g_Touches[i].active &&
		    g_Touches[i].touchId == touchId &&
		    g_Touches[i].fingerId == fingerId) {
			return i;
		}
	}
	return -1;
}

/*
 * Returns the slot for a pointer, reusing the one it already has if this is
 * a duplicate down event, so a pointer can never end up tracked twice.
 */
static inline s32 touchAllocPoint(SDL_TouchID touchId, SDL_FingerID fingerId)
{
	const s32 existing = touchFindPoint(touchId, fingerId);
	if (existing >= 0) {
		return existing;
	}
	for (s32 i = 0; i < TOUCH_MAX_POINTS; ++i) {
		if (!g_Touches[i].active) {
			return i;
		}
	}
	return -1;
}

void touchGetLeftStick(f32 *x, f32 *y)
{
	if (x) *x = g_StickX;
	if (y) *y = g_StickY;
}

void touchGetRightStick(f32 *x, f32 *y)
{
	/* The look area is relative, so there is no absolute stick value. */
	if (x) *x = 0.f;
	if (y) *y = 0.f;
}

u32 touchGetButtons(void)
{
	return g_Buttons;
}

u32 touchGetButtonsPressed(void)
{
	return g_ButtonsPressed;
}

/*
 * Returns 1 (and clears the flag) if a tap happened in a menu. The port's
 * input code uses this to press the game's accept button for one sample.
 */
s32 touchGetMenuAccept(void)
{
	const s32 v = g_MenuAccept;
	g_MenuAccept = 0;
	return v;
}

/*
 * Returns the buttons that were tapped since the last call, and clears them.
 * A tap is a press that was never confirmed as a hold, so it only lasts a
 * single input sample.
 */
u32 touchGetTapButton(void)
{
	const u32 v = g_TapButton;
	g_TapButton = 0;
	return v;
}

void touchSetWindow(SDL_Window *wnd)
{
	g_TouchWindow = wnd;
}

/*
 * Returns the look movement since the last call, and clears it.
 *
 * Only pointers that own the look area contribute, and each pointer's
 * movement is kept separately, so a pointer that owns the movement stick can
 * never turn the camera.
 */
void touchGetLookDelta(f32 *dx, f32 *dy)
{
	f32 totalX = 0.f, totalY = 0.f;

	for (s32 i = 0; i < TOUCH_MAX_POINTS; ++i) {
		if (!g_Touches[i].active || g_Touches[i].zone != ZONE_LOOK) {
			continue;
		}
		totalX += g_Touches[i].lookDX;
		totalY += g_Touches[i].lookDY;
		g_Touches[i].lookDX = 0.f;
		g_Touches[i].lookDY = 0.f;
	}

	if (dx) *dx = totalX;
	if (dy) *dy = totalY;
}

/*
 * ------------------------------------------------------------------
 * Layout editor
 * ------------------------------------------------------------------
 *
 * While the editor is open the buttons can be dragged around. The buttons
 * are not sent to the game, so arranging them cannot fire anything.
 */

#define EDIT_BTN_DONE 0
#define EDIT_BTN_RESET 1
#define EDIT_BTN_COUNT 2

static f32 g_EditBtnX[EDIT_BTN_COUNT];
static f32 g_EditBtnY[EDIT_BTN_COUNT];
static f32 g_EditBtnR[EDIT_BTN_COUNT];

/* The button currently being dragged, or -1. */
static s32 g_EditDrag = -1;
/* How far the finger is from the centre of the dragged button. */
static f32 g_EditGrabX = 0.f, g_EditGrabY = 0.f;

static void touchLayoutEditButtons(void)
{
	const f32 minDim = touchMinDim();
	const f32 unit = minDim * TOUCH_UNIT;
	const f32 r = 0.75f * unit * g_ButtonScale;

	/* Done and reset sit in the top centre, between the L and START buttons. */
	g_EditBtnX[EDIT_BTN_DONE] = (f32)g_ScreenW * 0.5f - unit * 1.9f;
	g_EditBtnY[EDIT_BTN_DONE] = unit * 1.4f;
	g_EditBtnR[EDIT_BTN_DONE] = r;
	g_EditBtnX[EDIT_BTN_RESET] = (f32)g_ScreenW * 0.5f + unit * 1.9f;
	g_EditBtnY[EDIT_BTN_RESET] = unit * 1.4f;
	g_EditBtnR[EDIT_BTN_RESET] = r;
}

/* Returns the edit button under the point, or -1. */
static s32 touchEditButtonAt(f32 x, f32 y)
{
	for (s32 i = 0; i < EDIT_BTN_COUNT; ++i) {
		const f32 dx = x - g_EditBtnX[i];
		const f32 dy = y - g_EditBtnY[i];
		const f32 hit = g_EditBtnR[i] * 1.25f;
		if (dx * dx + dy * dy <= hit * hit) {
			return i;
		}
	}
	/* Then any of the game buttons, so they can be dragged. */
	for (s32 i = 0; i < BTN_COUNT; ++i) {
		if (!g_Btns[i].inGame && !g_Btns[i].inMenu) {
			continue;
		}
		const f32 dx = x - g_BtnPixelX[i];
		const f32 dy = y - g_BtnPixelY[i];
		const f32 hit = g_BtnPixelR[i] * 1.25f;
		if (dx * dx + dy * dy <= hit * hit) {
			return BTN_COUNT + i;
		}
	}
	return -1;
}

static s32 touchHandleEditEvent(const SDL_Event *event, s32 w, s32 h)
{
	touchLayoutEditButtons();

	switch (event->type) {
		case SDL_FINGERDOWN: {
			const f32 x = event->tfinger.x * (f32)w;
			const f32 y = event->tfinger.y * (f32)h;
			const s32 hit = touchEditButtonAt(x, y);
			if (hit < 0) {
				return 1;
			}
			g_EditDrag = hit;
			if (hit < EDIT_BTN_COUNT) {
				g_EditGrabX = 0.f;
				g_EditGrabY = 0.f;
			} else {
				const s32 b = hit - EDIT_BTN_COUNT;
				g_EditGrabX = g_BtnPixelX[b] - x;
				g_EditGrabY = g_BtnPixelY[b] - y;
			}
			break;
		}
		case SDL_FINGERMOTION: {
			if (g_EditDrag < 0) {
				return 1;
			}
			const f32 x = event->tfinger.x * (f32)w;
			const f32 y = event->tfinger.y * (f32)h;
			if (g_EditDrag >= EDIT_BTN_COUNT) {
				/* Move the button under the finger. */
				const s32 b = g_EditDrag - EDIT_BTN_COUNT;
				const f32 nx = x + g_EditGrabX;
				const f32 ny = y + g_EditGrabY;
				/* Store the offset from the default position. */
				const f32 defX = g_BtnPixelX[b] - g_BtnOffX[b] * (f32)w;
				const f32 defY = g_BtnPixelY[b] - g_BtnOffY[b] * (f32)h;
				g_BtnOffX[b] = (nx - defX) / (f32)w;
				g_BtnOffY[b] = (ny - defY) / (f32)h;
				touchLayoutButtons();
			}
			break;
		}
		case SDL_FINGERUP: {
			if (g_EditDrag >= 0 && g_EditDrag < EDIT_BTN_COUNT) {
				const f32 x = event->tfinger.x * (f32)w;
				const f32 y = event->tfinger.y * (f32)h;
				const s32 hit = touchEditButtonAt(x, y);
				if (hit == EDIT_BTN_DONE) {
					g_EditMode = 0;
				} else if (hit == EDIT_BTN_RESET) {
					touchResetLayout();
					touchLayoutButtons();
				}
			}
			g_EditDrag = -1;
			break;
		}
		default:
			break;
	}
	return 1;
}

s32 touchHandleEvent(const SDL_Event *event)
{
	if (!g_Initialized || !g_TouchEnabled) {
		return 0;
	}

	s32 w = 0, h = 0;
	touchGetLayoutSize(&w, &h);
	if (w <= 0 || h <= 0) {
		return 0;
	}
	g_ScreenW = w;
	g_ScreenH = h;
	touchLayoutButtons();

	if (g_EditMode) {
		return touchHandleEditEvent(event, w, h);
	}

	switch (event->type) {
		case SDL_FINGERDOWN: {
			const SDL_TouchID touchId = event->tfinger.touchId;
			const SDL_FingerID fingerId = event->tfinger.fingerId;
			s32 idx = touchAllocPoint(touchId, fingerId);
			if (idx < 0) {
				return 1;
			}
			TouchPoint *t = &g_Touches[idx];
			t->active = 1;
			t->touchId = touchId;
			t->fingerId = fingerId;
			t->x = event->tfinger.x * (f32)w;
			t->y = event->tfinger.y * (f32)h;
			t->originX = t->x;
			t->originY = t->y;
			t->lookDX = 0.f;
			t->lookDY = 0.f;

			/*
			 * The zone is decided once, when the pointer goes down, and is
			 * kept until it lifts. Recomputing it on every motion event
			 * would let a drag wander onto a button and fire it.
			 */
			const s32 btn = touchButtonAt(t->x, t->y);
			t->pendingButton = -1;
			t->button = -1;
			if (btn >= 0) {
				/*
				 * The press is not confirmed yet. It only becomes a real
				 * press once the pointer has stayed still for a couple of
				 * frames; if it moves instead, the gesture is a drag and
				 * the button is dropped without ever reaching the game.
				 */
				t->zone = ZONE_BUTTON;
				t->pendingButton = btn;
				t->downFrame = g_Frame;
			} else if (event->tfinger.x < 0.5f) {
				/*
				 * The left half of the screen is the movement stick. This
				 * is decided from the normalised coordinate, so it does
				 * not depend on the surface size or its orientation.
				 */
				t->zone = ZONE_MOVE;
			} else {
				/* The right half of the screen looks around. */
				t->zone = ZONE_LOOK;
			}
			break;
		}
		case SDL_FINGERMOTION: {
			const SDL_TouchID touchId = event->tfinger.touchId;
			const SDL_FingerID fingerId = event->tfinger.fingerId;
			s32 idx = touchFindPoint(touchId, fingerId);
			if (idx < 0) {
				return 0;
			}
			TouchPoint *t = &g_Touches[idx];
			const f32 nx = event->tfinger.x * (f32)w;
			const f32 ny = event->tfinger.y * (f32)h;

			/*
			 * A pointer that starts on a button and then moves is a drag,
			 * not a button press. The button is dropped and the drag
			 * becomes camera movement, so looking around can never fire
			 * the gun.
			 */
			if (t->zone == ZONE_BUTTON) {
				const f32 dx = nx - t->originX;
				const f32 dy = ny - t->originY;
				const f32 limit = touchMinDim() * 0.06f;
				if (dx * dx + dy * dy > limit * limit) {
					if (t->button >= 0) {
						/* It had already been confirmed, so release it. */
						g_Buttons &= ~g_Btns[t->button].contBit;
						t->button = -1;
					}
					t->pendingButton = -1;
					t->zone = ZONE_LOOK;
				}
			}

			if (t->zone == ZONE_LOOK && !touchIsInMenu()) {
				/*
				 * Relative look. The movement is kept on this pointer
				 * only, so a pointer that owns the movement stick can
				 * never contribute to the camera.
				 *
				 * The delta is scaled relative to a 1280 pixel wide
				 * screen, so the same swipe turns the camera by the same
				 * amount on every device.
				 */
				const f32 norm = 1280.f / (f32)w;
				t->lookDX += (nx - t->x) * norm * g_LookSensX * g_AimSensitivity * TOUCH_LOOK_BASE_SENS;
				t->lookDY += (ny - t->y) * norm * g_LookSensY * g_AimSensitivity * TOUCH_LOOK_BASE_SENS;
			}
			t->x = nx;
			t->y = ny;
			break;
		}
		case SDL_FINGERUP: {
			const SDL_TouchID touchId = event->tfinger.touchId;
			const SDL_FingerID fingerId = event->tfinger.fingerId;
			s32 idx = touchFindPoint(touchId, fingerId);
			if (idx >= 0) {
				TouchPoint *t = &g_Touches[idx];
				if (t->pendingButton >= 0) {
					/*
					 * A tap: the press was never confirmed, so it is
					 * delivered now as a single press.
					 */
					g_TapButton |= g_Btns[t->pendingButton].contBit;
					g_ButtonsPressed |= g_Btns[t->pendingButton].contBit;
					t->pendingButton = -1;
				} else if (t->button >= 0) {
					/* It had been held, so release it. */
					g_Buttons &= ~g_Btns[t->button].contBit;
					t->button = -1;
				} else if (touchIsInMenu() && t->zone != ZONE_NONE) {
					/*
					 * In a menu the on-screen gamepad is hidden, so a tap
					 * stands in for the accept button. The pulse is
					 * consumed by the next input sample, so the game sees
					 * it as a single press.
					 */
					const f32 dx = t->x - t->originX;
					const f32 dy = t->y - t->originY;
					if (dx * dx + dy * dy < touchMinDim() * touchMinDim() * 0.01f) {
						g_MenuAccept = 1;
					}
				}
				t->active = 0;
				t->zone = ZONE_NONE;
				t->button = -1;
				t->pendingButton = -1;
				t->lookDX = 0.f;
				t->lookDY = 0.f;
			}
			break;
		}
		default:
			return 0;
	}

	/* Recompute the movement stick from the fingers that own it. */
	g_StickX = 0.f;
	g_StickY = 0.f;
	{
		f32 sumX = 0.f, sumY = 0.f;
		s32 count = 0;
		const f32 radius = touchMinDim() * 0.13f * g_StickSize;
		for (s32 i = 0; i < TOUCH_MAX_POINTS; ++i) {
			if (!g_Touches[i].active || g_Touches[i].zone != ZONE_MOVE) {
				continue;
			}
			f32 dx = g_Touches[i].x - g_Touches[i].originX;
			f32 dy = g_Touches[i].y - g_Touches[i].originY;
			const f32 dist = sqrtf(dx * dx + dy * dy);
			if (dist < g_StickDeadzone * radius) {
				continue;
			}
			if (dist > radius) {
				dx = dx * (radius / dist);
				dy = dy * (radius / dist);
			}
			sumX += dx / radius;
			/*
			 * Screen y grows downwards, but the port's controller code
			 * negates the raw axis, so "up" has to be positive here to
			 * match: pushing the stick forward must walk forward.
			 */
			sumY -= dy / radius;
			count++;
		}
		if (count) {
			f32 len = sqrtf(sumX * sumX + sumY * sumY);
			if (len > 1.f) { sumX /= len; sumY /= len; }
			g_StickX = sumX;
			g_StickY = sumY;
		}
	}

	/*
	 * The look movement is accumulated per pointer and is only collected by
	 * touchGetLookDelta(), once per frame. Collecting it here would wipe the
	 * movement of a pointer whenever another pointer (the movement stick,
	 * for instance) reported a motion event first.
	 */

	return 1;
}

/* Called once per frame, after the game has read the touch state. */
void touchEndFrame(void)
{
	g_ButtonsPressed = 0;
	g_Frame++;

	/*
	 * Confirm button presses that have been held still long enough. A
	 * finger that is still on a button after a couple of frames was a
	 * deliberate press, so it becomes a real held button.
	 */
	for (s32 i = 0; i < TOUCH_MAX_POINTS; ++i) {
		TouchPoint *t = &g_Touches[i];
		if (!t->active || t->pendingButton < 0) {
			continue;
		}
		if (g_Frame - t->downFrame >= TOUCH_PRESS_DELAY) {
			g_Buttons |= g_Btns[t->pendingButton].contBit;
			g_ButtonsPressed |= g_Btns[t->pendingButton].contBit;
			t->button = t->pendingButton;
			t->pendingButton = -1;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static const char *g_VertexSrc =
	"attribute vec2 aPos;\n"
	"attribute vec2 aUV;\n"
	"attribute vec4 aColor;\n"
	"uniform mat4 uProj;\n"
	"varying vec2 vUV;\n"
	"varying vec4 vColor;\n"
	"void main() {\n"
	"  gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
	"  vUV = aUV;\n"
	"  vColor = aColor;\n"
	"}\n";

static const char *g_FragmentSrc =
	"precision mediump float;\n"
	"uniform sampler2D uTex;\n"
	"varying vec2 vUV;\n"
	"varying vec4 vColor;\n"
	"void main() {\n"
	"  vec4 c = texture2D(uTex, vUV);\n"
	"  gl_FragColor = vec4(vColor.rgb, vColor.a * c.a);\n"
	"}\n";

static GLuint touchCompileShader(GLenum type, const char *src)
{
	GLuint shader = glCreateShader(type);
	if (!shader) {
		return 0;
	}
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		GLchar log[512];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		sysLogPrintf(LOG_ERROR, "touch shader compile failed: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

/*
 * A tiny 5x7 bitmap font, one byte per column, 7 bits per column (bit 0 is
 * the top row). Only the characters used by the button labels are present.
 */
static const u8 g_Font[5 * 7 * 16] = {
	/* '0' */ 0x3E,0x51,0x49,0x45,0x3E,
	/* '1' */ 0x00,0x42,0x7F,0x40,0x00,
	/* '2' */ 0x42,0x61,0x51,0x49,0x46,
	/* '3' */ 0x21,0x41,0x45,0x4B,0x31,
	/* '4' */ 0x18,0x14,0x12,0x7F,0x10,
	/* '5' */ 0x27,0x45,0x45,0x45,0x39,
	/* '6' */ 0x3C,0x4A,0x49,0x49,0x30,
	/* '7' */ 0x01,0x71,0x09,0x05,0x03,
	/* '8' */ 0x36,0x49,0x49,0x49,0x36,
	/* '9' */ 0x06,0x49,0x49,0x29,0x1E,
	/* 'A' */ 0x7E,0x11,0x11,0x11,0x7E,
	/* 'B' */ 0x7F,0x49,0x49,0x49,0x36,
	/* 'C' */ 0x3E,0x41,0x41,0x41,0x22,
	/* 'D' */ 0x7F,0x41,0x41,0x22,0x1C,
	/* 'E' */ 0x7F,0x49,0x49,0x49,0x41,
	/* 'F' */ 0x7F,0x09,0x09,0x09,0x01,
	/* 'G' */ 0x3E,0x41,0x49,0x49,0x7A,
	/* 'H' */ 0x7F,0x08,0x08,0x08,0x7F,
	/* 'I' */ 0x00,0x41,0x7F,0x41,0x00,
	/* 'J' */ 0x20,0x40,0x41,0x3F,0x01,
	/* 'K' */ 0x7F,0x08,0x14,0x22,0x41,
	/* 'L' */ 0x7F,0x40,0x40,0x40,0x40,
	/* 'M' */ 0x7F,0x02,0x0C,0x02,0x7F,
	/* 'N' */ 0x7F,0x04,0x08,0x10,0x7F,
	/* 'O' */ 0x3E,0x41,0x41,0x41,0x3E,
	/* 'P' */ 0x7F,0x09,0x09,0x09,0x06,
	/* 'Q' */ 0x3E,0x41,0x51,0x21,0x5E,
	/* 'R' */ 0x7F,0x09,0x19,0x29,0x46,
	/* 'S' */ 0x46,0x49,0x49,0x49,0x31,
	/* 'T' */ 0x01,0x01,0x7F,0x01,0x01,
	/* 'U' */ 0x3F,0x40,0x40,0x40,0x3F,
	/* 'V' */ 0x1F,0x20,0x40,0x20,0x1F,
	/* 'W' */ 0x3F,0x40,0x38,0x40,0x3F,
	/* 'X' */ 0x63,0x14,0x08,0x14,0x63,
	/* 'Y' */ 0x07,0x08,0x70,0x08,0x07,
	/* 'Z' */ 0x61,0x51,0x49,0x45,0x43,
	/* ' ' */ 0x00,0x00,0x00,0x00,0x00,
	/* '-' */ 0x08,0x08,0x08,0x08,0x08,
	/* '.' */ 0x00,0x60,0x60,0x00,0x00,
	/* '/' */ 0x20,0x10,0x08,0x04,0x02,
	/* ':' */ 0x00,0x36,0x36,0x00,0x00,
	/* '+' */ 0x08,0x08,0x3E,0x08,0x08,
	/* '?' */ 0x02,0x01,0x51,0x09,0x06,
	/* '%' */ 0x23,0x13,0x08,0x64,0x62,
};

/* Builds a lookup table mapping a character to its index in g_Font. */
static s32 touchFontIndex(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
	if (c == ' ')  return 36;
	if (c == '-')  return 37;
	if (c == '.')  return 38;
	if (c == '/')  return 39;
	if (c == ':')  return 40;
	if (c == '+')  return 41;
	if (c == '?')  return 42;
	if (c == '%')  return 43;
	return -1;
}

typedef struct {
	f32 x, y;
	f32 u, v;
	f32 r, g, b, a;
} TouchVertex;

static TouchVertex g_Verts[8192];
static s32 g_VertCount = 0;

static inline void touchVertex(f32 x, f32 y, f32 u, f32 v, f32 r, f32 g, f32 b, f32 a)
{
	if (g_VertCount >= (s32)(sizeof(g_Verts) / sizeof(g_Verts[0]))) {
		return;
	}
	TouchVertex *vert = &g_Verts[g_VertCount++];
	vert->x = x; vert->y = y;
	vert->u = u; vert->v = v;
	vert->r = r; vert->g = g; vert->b = b; vert->a = a;
}

static void touchRect(f32 x, f32 y, f32 hw, f32 hh, f32 r, f32 g, f32 b, f32 a)
{
	touchVertex(x - hw, y - hh, 0.f, 0.f, r, g, b, a);
	touchVertex(x + hw, y - hh, 1.f, 0.f, r, g, b, a);
	touchVertex(x + hw, y + hh, 1.f, 1.f, r, g, b, a);

	touchVertex(x - hw, y - hh, 0.f, 0.f, r, g, b, a);
	touchVertex(x + hw, y + hh, 1.f, 1.f, r, g, b, a);
	touchVertex(x - hw, y + hh, 0.f, 1.f, r, g, b, a);
}

/*
 * A soft, anti-aliased disc, drawn as a textured quad. The texture's alpha
 * fades out at the edge, which is what makes the buttons look round and
 * smooth instead of jagged.
 */
static void touchDisc(f32 cx, f32 cy, f32 rad, f32 r, f32 g, f32 b, f32 a)
{
	touchVertex(cx - rad, cy - rad, 0.0f, 0.0f, r, g, b, a);
	touchVertex(cx + rad, cy - rad, 0.5f, 0.0f, r, g, b, a);
	touchVertex(cx + rad, cy + rad, 0.5f, 1.0f, r, g, b, a);

	touchVertex(cx - rad, cy - rad, 0.0f, 0.0f, r, g, b, a);
	touchVertex(cx + rad, cy + rad, 0.5f, 1.0f, r, g, b, a);
	touchVertex(cx - rad, cy + rad, 0.0f, 1.0f, r, g, b, a);
}

/* A soft ring, drawn as a textured quad using the right half of the texture. */
static void touchSoftRing(f32 cx, f32 cy, f32 rad, f32 r, f32 g, f32 b, f32 a)
{
	touchVertex(cx - rad, cy - rad, 0.5f, 0.0f, r, g, b, a);
	touchVertex(cx + rad, cy - rad, 1.0f, 0.0f, r, g, b, a);
	touchVertex(cx + rad, cy + rad, 1.0f, 1.0f, r, g, b, a);

	touchVertex(cx - rad, cy - rad, 0.5f, 0.0f, r, g, b, a);
	touchVertex(cx + rad, cy + rad, 1.0f, 1.0f, r, g, b, a);
	touchVertex(cx - rad, cy + rad, 0.5f, 1.0f, r, g, b, a);
}

/* An arrow. dir: 0 = up, 1 = down, 2 = left, 3 = right. */
static void touchArrow(f32 cx, f32 cy, f32 rad, s32 dir, f32 r, f32 g, f32 b, f32 a)
{
	f32 dx = 0.f, dy = 0.f;
	switch (dir & 3) {
	case 0: dx = 0.f;  dy = -1.f; break;  // up
	case 1: dx = 0.f;  dy = 1.f;  break;  // down
	case 2: dx = -1.f; dy = 0.f;  break;  // left
	default: dx = 1.f; dy = 0.f; break;  // right
	}
	const f32 px = -dy, py = dx; // perpendicular

	const f32 tipx = cx + dx * rad * 0.85f;
	const f32 tipy = cy + dy * rad * 0.85f;
	const f32 lx = cx - dx * rad * 0.45f + px * rad * 0.65f;
	const f32 ly = cy - dy * rad * 0.45f + py * rad * 0.65f;
	const f32 rx = cx - dx * rad * 0.45f - px * rad * 0.65f;
	const f32 ry = cy - dy * rad * 0.45f - py * rad * 0.65f;

	touchVertex(tipx, tipy, 0.5f, 0.5f, r, g, b, a);
	touchVertex(lx, ly, 0.5f, 0.5f, r, g, b, a);
	touchVertex(rx, ry, 0.5f, 0.5f, r, g, b, a);
}

/*
 * Draws a string of text. Returns the width in pixels.
 *
 * The font is a 5x7 grid, and every font pixel is drawn as a square, so the
 * glyphs keep their natural proportions instead of being stretched.
 */
static f32 touchText(f32 x, f32 y, f32 scale, const char *text, f32 r, f32 g, f32 b, f32 a)
{
	const f32 p = scale / 5.f;    // size of one font pixel
	const f32 advance = p * 6.f;   // five pixels plus one of spacing
	f32 cx = x;

	for (const char *ch = text; *ch; ++ch) {
		const s32 gi = touchFontIndex(*ch);
		if (gi < 0) {
			cx += advance;
			continue;
		}
		for (s32 col = 0; col < 5; ++col) {
			const u8 bits = g_Font[gi * 5 + col];
			for (s32 row = 0; row < 7; ++row) {
				if (!(bits & (1 << row))) {
					continue;
				}
				const f32 px = cx + col * p;
				const f32 py = y + row * p;
				touchRect(px + p * 0.5f, py + p * 0.5f, p * 0.5f, p * 0.5f, r, g, b, a);
			}
		}
		cx += advance;
	}
	return cx - x;
}

static f32 touchTextWidth(f32 scale, const char *text)
{
	f32 n = 0;
	for (const char *ch = text; *ch; ++ch) {
		n += 1;
	}
	return n * scale * 1.2f;
}

/* ------------------------------------------------------------------ */

void touchInit(void)
{
	if (g_Initialized) {
		return;
	}

	GLuint vs = touchCompileShader(GL_VERTEX_SHADER, g_VertexSrc);
	GLuint fs = touchCompileShader(GL_FRAGMENT_SHADER, g_FragmentSrc);
	if (!vs || !fs) {
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return;
	}

	g_Shader = glCreateProgram();
	glAttachShader(g_Shader, vs);
	glAttachShader(g_Shader, fs);
	glLinkProgram(g_Shader);
	GLint ok = 0;
	glGetProgramiv(g_Shader, GL_LINK_STATUS, &ok);
	if (!ok) {
		GLchar log[512];
		glGetProgramInfoLog(g_Shader, sizeof(log), NULL, log);
		sysLogPrintf(LOG_ERROR, "touch program link failed: %s", log);
		glDeleteProgram(g_Shader);
		glDeleteShader(vs);
		glDeleteShader(fs);
		g_Shader = 0;
		return;
	}

	glDetachShader(g_Shader, vs);
	glDetachShader(g_Shader, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);

	g_LocProj = glGetUniformLocation(g_Shader, "uProj");
	g_LocPos = glGetAttribLocation(g_Shader, "aPos");
	g_LocUV = glGetAttribLocation(g_Shader, "aUV");
	g_LocColor = glGetAttribLocation(g_Shader, "aColor");
	g_LocTex = glGetUniformLocation(g_Shader, "uTex");

	/* Vertex array objects: core in GLES3, an extension in GLES2. */
	pfnGenVertexArrays = (PFN_touchGenVertexArrays)SDL_GL_GetProcAddress("glGenVertexArrays");
	pfnBindVertexArray = (PFN_touchBindVertexArray)SDL_GL_GetProcAddress("glBindVertexArray");
	if (!pfnGenVertexArrays) {
		pfnGenVertexArrays = (PFN_touchGenVertexArrays)SDL_GL_GetProcAddress("glGenVertexArraysOES");
	}
	if (!pfnBindVertexArray) {
		pfnBindVertexArray = (PFN_touchBindVertexArray)SDL_GL_GetProcAddress("glBindVertexArrayOES");
	}
	if (pfnGenVertexArrays && pfnBindVertexArray) {
		pfnGenVertexArrays(1, &g_Vao);
	}

	/*
	 * A 128x64 texture with two shapes side by side:
	 *   left half  (u 0.0 .. 0.5) - a soft filled disc
	 *   right half (u 0.5 .. 1.0) - a soft ring
	 * Both have an anti-aliased edge, and the disc carries a vertical
	 * gradient so the buttons look like they have a lit top surface.
	 */
	{
		enum { TEXW = 128, TEXH = 64 };
		u8 tex[TEXH * TEXW * 4];
		for (s32 y = 0; y < TEXH; ++y) {
			for (s32 x = 0; x < TEXW; ++x) {
				u8 *px = &tex[(y * TEXW + x) * 4];
				const s32 isRing = (x >= TEXW / 2);
				const f32 cx = isRing ? (f32)(x - TEXW / 2) + 0.5f : (f32)x + 0.5f;
				const f32 cy = (f32)y + 0.5f;
				const f32 dx = (cx - 32.f) / 32.f;
				const f32 dy = (cy - 32.f) / 32.f;
				const f32 dist = sqrtf(dx * dx + dy * dy);

				f32 a;
				if (isRing) {
					/* A ring between r = 0.72 and r = 0.94. */
					if (dist < 0.72f) {
						a = 0.f;
					} else if (dist < 0.86f) {
						a = 1.f;
					} else if (dist < 0.98f) {
						a = 1.f - (dist - 0.86f) / 0.12f;
					} else {
						a = 0.f;
					}
				} else {
					/* A filled disc with a soft edge. */
					if (dist < 0.88f) {
						a = 1.f;
					} else if (dist < 1.f) {
						a = 1.f - (dist - 0.88f) / 0.12f;
					} else {
						a = 0.f;
					}
				}

				/* Vertical gradient: brighter at the top. */
				const f32 grad = 1.f - (cy / (f32)TEXH) * 0.30f;
				px[0] = (u8)(255.f * grad);
				px[1] = (u8)(255.f * grad);
				px[2] = (u8)(255.f * grad);
				px[3] = (u8)(255.f * a);
			}
		}
		glGenTextures(1, &g_Tex);
		glBindTexture(GL_TEXTURE_2D, g_Tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXW, TEXH, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/* Vertex buffer for the overlay geometry. */
	glGenBuffers(1, &g_Vbo);

	g_Initialized = 1;
	sysLogPrintf(LOG_NOTE, "touch controls initialized (vao: %s)",
		g_Vao ? "yes" : "no");
}

void touchRenderOverlay(void)
{
	if (!g_Initialized || !g_Shader) {
		return;
	}
	if (!g_TouchEnabled) {
		return;
	}

	/*
	 * The gamepad is hidden while a menu, a pause screen or a cutscene is
	 * showing, so it does not cover the interface. In a menu only a small
	 * confirm/back pair is drawn instead. The layout editor always shows
	 * everything, since that is what it is for.
	 */
	const s32 inMenu = touchIsInMenu() && !g_EditMode;

	/* Always use the real on-screen size. */
	s32 w = 0, h = 0;
	touchGetLayoutSize(&w, &h);
	if (w <= 0 || h <= 0) {
		return;
	}
	g_ScreenW = w;
	g_ScreenH = h;
	touchLayoutButtons();

	/* Log the sizes once, so a device that lays the controls out wrongly can
	   be identified from the log. */
	if (!g_LoggedSizes) {
		g_LoggedSizes = 1;
		s32 surfW = 0, surfH = 0;
		touchGetSurfaceSize(&surfW, &surfH);
		sysLogPrintf(LOG_NOTE, "touch: surface %dx%d, framebuffer %dx%d, unit %dpx",
			surfW, surfH, w, h, (s32)(touchMinDim() * TOUCH_UNIT));
	}

	/*
	 * The overlay must not touch the game's vertex array state: the game keeps
	 * its attributes (aVtxPos, aTexCoord0, ...) enabled in its own VAO, and
	 * disabling them would make the whole game render black.
	 */
	if (!g_Vao || !pfnBindVertexArray) {
		return;
	}

	// save GL state
	GLint curProgram = 0, curTex = 0, curFBO = 0, curArrayBuf = 0;
	GLint viewport[4] = { 0, 0, 0, 0 };
	GLboolean blend = GL_FALSE, depthTest = GL_FALSE, scissor = GL_FALSE;
	glGetIntegerv(GL_CURRENT_PROGRAM, &curProgram);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &curTex);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &curFBO);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &curArrayBuf);
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetBooleanv(GL_BLEND, &blend);
	glGetBooleanv(GL_DEPTH_TEST, &depthTest);
	glGetBooleanv(GL_SCISSOR_TEST, &scissor);

	// orthographic projection (screen px origin top-left -> NDC)
	f32 proj[16] = {
		2.f / (f32)w, 0.f, 0.f, 0.f,
		0.f, -2.f / (f32)h, 0.f, 0.f,
		0.f, 0.f, 1.f, 0.f,
		-1.f, 1.f, 0.f, 1.f,
	};

	g_VertCount = 0;

	const f32 alpha = g_OverlayOpacity;
	const f32 minDim = touchMinDim();
	const f32 unit = minDim * TOUCH_UNIT;

	/* Movement stick: shown where the finger went down, if it is down. It is
	   not drawn in menus, where the direction pad is used instead. */
	for (s32 i = 0; !inMenu && i < TOUCH_MAX_POINTS; ++i) {
		if (!g_Touches[i].active || g_Touches[i].zone != ZONE_MOVE) {
			continue;
		}
		const f32 radius = minDim * 0.13f * g_StickSize;
		const f32 ox = g_Touches[i].originX;
		const f32 oy = g_Touches[i].originY;
		/*
		 * The stick's Y axis is "up is positive", but the screen's Y axis
		 * grows downwards, so it has to be flipped here for the knob to
		 * line up with the direction the player is actually pushing.
		 */
		const f32 knobX = ox + g_StickX * radius * 0.55f;
		const f32 knobY = oy - g_StickY * radius * 0.55f;
		/* A soft shadow under the stick, then the ring, then the knob. */
		touchSoftRing(ox, oy + radius * 0.06f, radius * 1.02f, 0.f, 0.f, 0.f, alpha * 0.30f);
		touchSoftRing(ox, oy, radius * 1.0f, 0.80f, 0.88f, 1.0f, alpha * 0.75f);
		touchDisc(knobX, knobY + radius * 0.04f, radius * 0.28f, 0.f, 0.f, 0.f, alpha * 0.35f);
		touchDisc(knobX, knobY - radius * 0.04f, radius * 0.25f, 0.85f, 0.90f, 1.0f, alpha * 0.95f);
	}

	/* The zone hints, when enabled. */
	if (g_ShowZones && !inMenu) {
		touchSoftRing((f32)w * 0.25f, (f32)h * 0.72f, minDim * 0.13f * g_StickSize,
			0.6f, 0.7f, 0.9f, alpha * 0.30f);
		touchSoftRing((f32)w * 0.78f, (f32)h * 0.72f, minDim * 0.13f * g_StickSize,
			0.9f, 0.7f, 0.6f, alpha * 0.30f);
	}

	/* Buttons, drawn back to front so the bigger ones sit on top. */
	for (s32 pass = 0; pass < 2; ++pass) {
		for (s32 i = 0; i < BTN_COUNT; ++i) {
			if (!touchButtonVisible(i)) {
				continue;
			}
			const TouchButton *b = &g_Btns[i];
			const s32 pressed = (g_Buttons & b->contBit) != 0;
			const f32 r = g_BtnPixelR[i];
			const f32 x = g_BtnPixelX[i];
			const f32 y = g_BtnPixelY[i];
			const s32 isArrow = (i >= BTN_MENU_UP && i <= BTN_MENU_RIGHT);

			/* The first pass draws the shadows, the second the buttons. */
			if (b->shoulder) {
				if (pass == 0) {
					touchRect(x, y + unit * 0.10f, r * 1.75f, r * 0.70f, 0.f, 0.f, 0.f, alpha * 0.32f);
				} else {
					touchRect(x, y, r * 1.70f, r * 0.62f, b->r * 0.45f, b->g * 0.45f, b->b * 0.45f, alpha);
					touchRect(x, y - r * 0.12f, r * 1.60f, r * 0.45f, b->r, b->g, b->b, pressed ? 1.f : alpha * 0.92f);
				}
			} else if (isArrow) {
				/* A direction pad, drawn as a disc with an arrow on it. */
				const s32 dir = i - BTN_MENU_UP; /* 0 = up, 1 = down, 2 = left, 3 = right */
				if (pass == 0) {
					touchDisc(x, y + unit * 0.12f, r * 1.04f, 0.f, 0.f, 0.f, alpha * 0.32f);
				} else {
					touchDisc(x, y, r, b->r * 0.30f, b->g * 0.30f, b->b * 0.30f, alpha * 0.80f);
					touchSoftRing(x, y, r * 0.98f, 1.f, 1.f, 1.f, alpha * 0.22f);
					touchArrow(x, y, r * 0.55f, dir, b->r, b->g, b->b, pressed ? 1.f : alpha);
				}
			} else if (pass == 0) {
				/* Drop shadow. */
				touchDisc(x, y + unit * 0.14f, r * 1.06f, 0.f, 0.f, 0.f, alpha * 0.35f);
			} else {
				/* Dark base, bright rim, then the coloured face. */
				touchDisc(x, y, r * 1.04f, b->r * 0.25f, b->g * 0.25f, b->b * 0.25f, alpha * 0.85f);
				touchSoftRing(x, y, r * 1.02f, 1.f, 1.f, 1.f, alpha * 0.30f);
				touchDisc(x, y, r * 0.92f, b->r, b->g, b->b, pressed ? 1.f : alpha * 0.88f);
				if (pressed) {
					/* A highlight when the button is held. */
					touchSoftRing(x, y, r * 0.98f, 1.f, 1.f, 1.f, 0.55f);
				}
			}

			if (g_ShowGamepad || b->inMenu) {
				if (!b->shoulder) {
					const f32 scale = r * 0.17f;
					const f32 tw = touchTextWidth(scale, b->label);
					const f32 tx = x - tw * 0.5f;
					const f32 ty = y - scale * 0.7f;
					/* A dark outline under the label so it stays readable. */
					touchText(tx - scale * 0.12f, ty - scale * 0.12f, scale, b->label, 0.f, 0.f, 0.f, alpha * 0.8f);
					touchText(tx + scale * 0.12f, ty - scale * 0.12f, scale, b->label, 0.f, 0.f, 0.f, alpha * 0.8f);
					touchText(tx - scale * 0.12f, ty + scale * 0.12f, scale, b->label, 0.f, 0.f, 0.f, alpha * 0.8f);
					touchText(tx + scale * 0.12f, ty + scale * 0.12f, scale, b->label, 0.f, 0.f, 0.f, alpha * 0.8f);
					touchText(tx, ty, scale, b->label, 1.f, 1.f, 1.f, alpha);
				}
			}
		}
	}

	/* The layout editor's own controls. */
	if (g_EditMode) {
		touchLayoutEditButtons();
		static const char *g_EditLabels[EDIT_BTN_COUNT] = { "DONE", "RESET" };
		for (s32 i = 0; i < EDIT_BTN_COUNT; ++i) {
			const f32 x = g_EditBtnX[i];
			const f32 y = g_EditBtnY[i];
			const f32 r = g_EditBtnR[i];
			touchDisc(x, y + unit * 0.12f, r * 1.04f, 0.f, 0.f, 0.f, alpha * 0.35f);
			touchDisc(x, y, r * 1.04f, 0.15f, 0.16f, 0.20f, alpha * 0.9f);
			touchSoftRing(x, y, r * 1.02f, 1.f, 1.f, 1.f, alpha * 0.35f);
			touchDisc(x, y, r * 0.92f, 0.30f, 0.85f, 0.45f, alpha * 0.95f);
			const f32 scale = r * 0.17f;
			const f32 tw = touchTextWidth(scale, g_EditLabels[i]);
			const f32 tx = x - tw * 0.5f;
			const f32 ty = y - scale * 0.7f;
			touchText(tx - scale * 0.12f, ty - scale * 0.12f, scale, g_EditLabels[i], 0.f, 0.f, 0.f, alpha * 0.8f);
			touchText(tx + scale * 0.12f, ty - scale * 0.12f, scale, g_EditLabels[i], 0.f, 0.f, 0.f, alpha * 0.8f);
			touchText(tx - scale * 0.12f, ty + scale * 0.12f, scale, g_EditLabels[i], 0.f, 0.f, 0.f, alpha * 0.8f);
			touchText(tx + scale * 0.12f, ty + scale * 0.12f, scale, g_EditLabels[i], 0.f, 0.f, 0.f, alpha * 0.8f);
			touchText(tx, ty, scale, g_EditLabels[i], 1.f, 1.f, 1.f, alpha);
		}
	}

	// upload and draw
	glUseProgram(g_Shader);
	glUniformMatrix4fv(g_LocProj, 1, GL_FALSE, proj);

	GLint savedVAO = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &savedVAO);
	pfnBindVertexArray(g_Vao);

	glBindBuffer(GL_ARRAY_BUFFER, g_Vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(TouchVertex) * (size_t)g_VertCount, g_Verts, GL_STREAM_DRAW);

	glEnableVertexAttribArray(g_LocPos);
	glEnableVertexAttribArray(g_LocUV);
	glEnableVertexAttribArray(g_LocColor);
	glVertexAttribPointer(g_LocPos, 2, GL_FLOAT, GL_FALSE, sizeof(TouchVertex), (void*)0);
	glVertexAttribPointer(g_LocUV, 2, GL_FLOAT, GL_FALSE, sizeof(TouchVertex), (void*)(sizeof(f32) * 2));
	glVertexAttribPointer(g_LocColor, 4, GL_FLOAT, GL_FALSE, sizeof(TouchVertex), (void*)(sizeof(f32) * 4));

	glViewport(0, 0, w, h);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_SCISSOR_TEST);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_Tex);
	glUniform1i(g_LocTex, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDrawArrays(GL_TRIANGLES, 0, g_VertCount);

	// restore GL state
	glUseProgram(curProgram);
	glBindTexture(GL_TEXTURE_2D, (GLuint)curTex);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)curFBO);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

	/* Give the game's vertex array object and array buffer back. */
	pfnBindVertexArray((GLuint)savedVAO);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)curArrayBuf);
}

#endif /* __ANDROID__ */
