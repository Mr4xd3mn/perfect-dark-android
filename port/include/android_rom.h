#ifndef _IN_ANDROID_ROM_H
#define _IN_ANDROID_ROM_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __ANDROID__

/*
 * Returns the path of the ROM in the app's internal storage, or NULL if the
 * player has not supplied one yet.
 */
const char *androidRomPath(const char *name);

/*
 * Makes sure a ROM is available, asking the player to pick one if it is
 * not. This blocks until the picker has been answered.
 *
 * There is no time limit: the player can browse for as long as they like,
 * and cancelling the picker opens it again. The wait only ends when a file
 * has been picked, or when the player gives up.
 *
 * Returns 0 on success and -1 if the player gave up.
 */
s32 androidRomEnsure(const char *name);

/* Returns 1 while the game is still waiting for the player to pick a ROM. */
s32 androidRomPending(void);

/* Returns 1 if the player chose to leave instead of picking a ROM. */
s32 androidRomGaveUp(void);

/* Stops waiting for the ROM, so that the app can close. */
void androidRomGiveUp(void);

#endif /* __ANDROID__ */

#ifdef __cplusplus
}
#endif

#endif /* _IN_ANDROID_ROM_H */
