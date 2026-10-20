/*
 * Getting the ROM onto an Android device.
 *
 * The ROM is copyrighted, so it is not shipped with the app. Instead the
 * player is asked to pick their own copy with the system file picker the
 * first time the game starts, and it is copied into the app's internal
 * storage where the port can find it.
 */

#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_system.h>

#include "platform.h"
#include "system.h"
#include "android_rom.h"

#ifdef __ANDROID__

#define ANDROID_ROM_JAVA_CLASS "org/sdl2/perfectdark/SDLActivity"

static jobject g_activity = NULL;   /* global ref */
static jmethodID g_pickRomMethod = NULL;

static SDL_mutex *g_romMutex = NULL;
static SDL_cond *g_romCond = NULL;
static s32 g_romPickDone = 0;
static s32 g_romPickOk = 0;
static s32 g_romPending = 0;   /* the game is waiting for a ROM */
static s32 g_romGaveUp = 0;    /* the player chose to leave */
static char g_romName[128] = "";

/* Asks the Java side to show the file picker. */
static s32 androidRomAskPicker(const char *name)
{
	if (!g_activity || !g_pickRomMethod) {
		return -1;
	}

	JNIEnv *env = SDL_AndroidGetJNIEnv();
	if (!env) {
		return -1;
	}

	jstring jname = (*env)->NewStringUTF(env, name);
	(*env)->CallVoidMethod(env, g_activity, g_pickRomMethod, jname);
	(*env)->DeleteLocalRef(env, jname);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		return -1;
	}

	return 0;
}

/* Called from Java when the activity is created. */
JNIEXPORT jint JNICALL
Java_org_sdl2_perfectdark_SDLActivity_nativeSetActivity(JNIEnv *env, jclass cls, jobject activity)
{
	if (!env || !activity) {
		return -1;
	}

	if (g_activity) {
		(*env)->DeleteGlobalRef(env, g_activity);
	}
	g_activity = (*env)->NewGlobalRef(env, activity);

	jclass actCls = (*env)->GetObjectClass(env, g_activity);
	if (actCls) {
		g_pickRomMethod = (*env)->GetMethodID(env, actCls, "pickRom", "(Ljava/lang/String;)V");
		(*env)->DeleteLocalRef(env, actCls);
	}

	if (!g_romMutex) {
		g_romMutex = SDL_CreateMutex();
		g_romCond = SDL_CreateCond();
	}

	/*
	 * If the ROM is still being picked - the activity was recreated, for
	 * example - open the file picker again, so the player is not left
	 * staring at a black screen.
	 */
	if (g_romPending && !g_romPickDone && g_romName[0]) {
		sysLogPrintf(LOG_NOTE, "android: reopening the file picker");
		androidRomAskPicker(g_romName);
	}

	return 0;
}

/* Called from Java when the file picker has been answered. */
JNIEXPORT void JNICALL
Java_org_sdl2_perfectdark_SDLActivity_nativeOnRomPickResult(JNIEnv *env, jclass cls, jboolean success)
{
	(void)env;
	(void)cls;

	if (!g_romMutex) {
		return;
	}

	SDL_LockMutex(g_romMutex);
	g_romPickOk = success ? 1 : 0;
	g_romPickDone = 1;
	g_romPending = 0;
	SDL_CondBroadcast(g_romCond);
	SDL_UnlockMutex(g_romMutex);
}

const char *androidRomPath(const char *name)
{
	static char path[1024];

	const char *dir = SDL_AndroidGetInternalStoragePath();
	if (!dir || !*dir || !name || !*name) {
		return NULL;
	}

	snprintf(path, sizeof(path), "%s/%s", dir, name);

	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	fclose(f);

	return path;
}

s32 androidRomEnsure(const char *name)
{
	if (androidRomPath(name)) {
		return 0;
	}

	if (!g_activity || !g_pickRomMethod) {
		sysLogPrintf(LOG_ERROR, "android: no activity available to ask for the ROM");
		return -1;
	}

	if (!g_romMutex) {
		g_romMutex = SDL_CreateMutex();
		g_romCond = SDL_CreateCond();
	}
	if (!g_romMutex || !g_romCond) {
		return -1;
	}

	strncpy(g_romName, name, sizeof(g_romName) - 1);
	g_romName[sizeof(g_romName) - 1] = '\0';

	SDL_LockMutex(g_romMutex);
	g_romPickDone = 0;
	g_romPickOk = 0;
	g_romPending = 1;
	SDL_UnlockMutex(g_romMutex);

	sysLogPrintf(LOG_NOTE, "android: asking the player for %s", name);

	if (androidRomAskPicker(name)) {
		SDL_LockMutex(g_romMutex);
		g_romPending = 0;
		SDL_UnlockMutex(g_romMutex);
		return -1;
	}

	/*
	 * Wait for the player to pick a file.
	 *
	 * There is no time limit at all: the player can browse for as long as
	 * they like. Cancelling the file picker does not end the wait either -
	 * the Java side opens the picker again. The wait only ends when a file
	 * has been picked, or when the player gives up and leaves the app.
	 */
	SDL_LockMutex(g_romMutex);
	while (!g_romPickDone) {
		SDL_CondWait(g_romCond, g_romMutex);
	}
	const s32 ok = g_romPickOk;
	g_romPending = 0;
	SDL_UnlockMutex(g_romMutex);

	if (!ok) {
		sysLogPrintf(LOG_WARNING, "android: the player did not supply a ROM");
		return -1;
	}

	return androidRomPath(name) ? 0 : -1;
}

/* Returns 1 while the game is still waiting for the player to pick a ROM. */
s32 androidRomPending(void)
{
	return g_romPending;
}

/* Returns 1 if the player chose to leave instead of picking a ROM. */
s32 androidRomGaveUp(void)
{
	return g_romGaveUp;
}

/* Stops waiting for the ROM, so that the app can close. */
void androidRomGiveUp(void)
{
	if (!g_romMutex) {
		return;
	}

	SDL_LockMutex(g_romMutex);
	g_romPickOk = 0;
	g_romPickDone = 1;
	g_romPending = 0;
	g_romGaveUp = 1;
	SDL_CondBroadcast(g_romCond);
	SDL_UnlockMutex(g_romMutex);
}

#endif /* __ANDROID__ */
