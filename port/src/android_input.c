/*
 * Android-specific input glue for the Perfect Dark port.
 *
 * Handles the system back button. It is mapped onto the game's own "back"
 * action, which the game binds to Escape:
 *   - src/game/menu.c, "ESC always acts as back"
 *   - src/game/bondmove.c, "always pause with ESC"
 *
 * If nothing acts on it within a short time, the activity is closed so the
 * user is not trapped in the app.
 *
 * Also provides haptics for the on-screen buttons.
 */

#include <jni.h>
#include <SDL.h>
#include <SDL_system.h>
#include <PR/ultratypes.h>
#include <PR/os_thread.h>

#include "platform.h"
#include "system.h"
#include "input.h"
#include "touch.h"
#include "android_rom.h"

#ifdef __ANDROID__

/* Frames to wait for the game to act on a back press before giving up. */
#define BACK_TIMEOUT_FRAMES 90

static s32 g_backPending = 0;
static u32 g_backKey = 0;
static u32 g_backAge = 0;

static void androidRequestExit(void);

/* Called from Java when the hardware back button is pressed. */
JNIEXPORT void JNICALL
Java_org_sdl2_perfectdark_SDLActivity_nativeBackPressed(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;

	/*
	 * If the game has not started yet - it is still waiting for the player
	 * to pick the ROM - there is nothing for the back button to do, so it
	 * leaves the app instead.
	 */
	if (androidRomPending()) {
		androidRomGiveUp();
		androidRequestExit();
		return;
	}

	/*
	 * Escape is the game's back action: it closes menus, and during
	 * gameplay it opens the pause menu.
	 */
	g_backKey = VK_ESCAPE;

	inputForceKey(g_backKey);
	g_backPending = 1;
	g_backAge = 0;
}

static void androidRequestExit(void)
{
	/* Ask SDL to close the activity for us. */
	SDL_AndroidBackButton();
}

/* Short haptic buzz when an on-screen button is pressed. */
static void androidHapticBuzz(s32 ms)
{
	if (ms <= 0) {
		return;
	}
	JNIEnv *env = SDL_AndroidGetJNIEnv();
	if (!env) {
		return;
	}
	JavaVM *vm = (JavaVM *)SDL_AndroidGetActivity();
	(void)vm;

	/* Use the activity to reach the vibrator service. */
	jobject activity = (jobject)SDL_AndroidGetActivity();
	if (!activity) {
		return;
	}
	jclass actCls = (*env)->GetObjectClass(env, activity);
	if (!actCls) {
		return;
	}
	jmethodID getSys = (*env)->GetMethodID(env, actCls, "getSystemService",
	                                       "(Ljava/lang/String;)Ljava/lang/Object;");
	if (getSys) {
		jstring svc = (*env)->NewStringUTF(env, "vibrator");
		jobject sys = (*env)->CallObjectMethod(env, activity, getSys, svc);
		(*env)->DeleteLocalRef(env, svc);
		if (sys) {
			jclass vibCls = (*env)->GetObjectClass(env, sys);
			if (vibCls) {
				jmethodID vibrate = (*env)->GetMethodID(env, vibCls, "vibrate", "(J)V");
				if (vibrate) {
					(*env)->CallVoidMethod(env, sys, vibrate, (jlong)ms);
				}
				(*env)->DeleteLocalRef(env, vibCls);
			}
			(*env)->DeleteLocalRef(env, sys);
		}
	}
	(*env)->DeleteLocalRef(env, actCls);
}

/* Called once per frame from inputUpdate(). */
void androidInputUpdate(void)
{
	if (g_backPending) {
		if (inputWasForcedKeyConsumed(g_backKey)) {
			/* the game handled it */
			g_backPending = 0;
			inputClearForcedKey(g_backKey);
		} else if (++g_backAge > BACK_TIMEOUT_FRAMES) {
			/* nothing acted on it, so leave the game */
			g_backPending = 0;
			inputClearForcedKey(g_backKey);
			androidRequestExit();
		}
	}

	/* Haptics for the on-screen buttons. */
	if (touchGetHaptics()) {
		const u32 pressed = touchGetButtonsPressed();
		if (pressed) {
			androidHapticBuzz(12);
		}
	}
	touchEndFrame();
}

#endif /* __ANDROID__ */
