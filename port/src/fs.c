#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>
#include "config.h"
#include "system.h"
#include "platform.h"
#include "utils.h"
#include "fs.h"
#ifdef __ANDROID__
#include <SDL.h>
#endif
#ifdef PLATFORM_WIN32
#include <direct.h>
#endif

#define DEFAULT_BASEDIR_NAME "data"

static char baseDir[FS_MAXPATH + 1]; // replaces $B
static char modDir[FS_MAXPATH + 1];  // replaces $M
static char saveDir[FS_MAXPATH + 1]; // replaces $S
static char homeDir[FS_MAXPATH + 1]; // replaces $H
static char exeDir[FS_MAXPATH + 1];  // replaces $E

#ifdef __ANDROID__
/*
 * On Android all game data is read from the APK's asset directory. SDL2
 * routes SDL_RWFromFile() to the Android asset system (AAssetManager) for
 * relative paths, so we use it here instead of talking to AAssetManager
 * directly. That keeps the port independent of SDL2's internal JNI.
 */
static s32 fsAssetSize(const char *name)
{
	SDL_RWops *rw = SDL_RWFromFile(name, "rb");
	if (!rw) {
		return -1;
	}
	const Sint64 size = SDL_RWsize(rw);
	SDL_RWclose(rw);
	return size < 0 ? -1 : (s32)size;
}

static s32 fsAssetLoad(const char *name, void *dst, u32 dstSize)
{
	SDL_RWops *rw = SDL_RWFromFile(name, "rb");
	if (!rw) {
		return -1;
	}
	const Sint64 size = SDL_RWsize(rw);
	if (size <= 0 || (u64)size > dstSize) {
		SDL_RWclose(rw);
		return -1;
	}
	const size_t read = SDL_RWread(rw, dst, 1, (size_t)size);
	SDL_RWclose(rw);
	return read == (size_t)size ? (s32)read : -1;
}
#endif

static s32 fsPathIsWritable(const char *path)
{
#ifdef PLATFORM_WIN32
	// on windows access() on directories will only check if the directory exists, so
	char tmp[FS_MAXPATH + 1] = { 0 };
	snprintf(tmp, sizeof(tmp), "%s/.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (f) {
		fclose(f);
		remove(tmp);
		return 1;
	}
	return 0;
#else
	return (access(path, W_OK) == 0);
#endif
}

s32 fsPathIsAbsolute(const char *path)
{
 return (path[0] == '/' || (isalpha(path[0]) && path[1] == ':'));
}

s32 fsPathIsCwdRelative(const char *path)
{
	// ., .., ./, ../
	return (path[0] == '.' && (path[1] == '.' || path[1] == '/' || path[1] == '\\' || path[1] == '\0'));
}

const char *fsFullPath(const char *relPath)
{
	static char pathBuf[FS_MAXPATH + 1];

	if (relPath[0] == '$') {
		// expandable placeholder $X; will be replaced with the corresponding path, if any
		const char *expStr = NULL;
		switch (relPath[1]) {
			case 'E': expStr = exeDir; break;
			case 'H': expStr = homeDir; break;
			case 'M': expStr = modDir; break;
			case 'B': expStr = baseDir; break;
			case 'S': expStr = saveDir; break;
			default: break;
		}
		if (expStr) {
			const u32 len = strlen(expStr);
			if (len > 0) {
				memcpy(pathBuf, expStr, len);
				strncpy(pathBuf + len, relPath + 2, FS_MAXPATH - len);
				return pathBuf;
			}
		}
		// couldn't expand anything, return as is
		return relPath;
	} else if (!baseDir[0] || fsPathIsAbsolute(relPath) || fsPathIsCwdRelative(relPath)) {
		// user explicitly wants working directory or this is an absolute path or we have no baseDir set up yet
		return relPath;
	}

	// path relative to mod or base dir; this will be a read request, so check where the file actually is
	if (modDir[0]) {
		snprintf(pathBuf, FS_MAXPATH, "%s/%s", modDir, relPath);
		if (fsFileSize(pathBuf) >= 0) {
			return pathBuf;
		}
	}
	// fall back to basedir
	snprintf(pathBuf, FS_MAXPATH, "%s/%s", baseDir, relPath);
	return pathBuf;
}

s32 fsInit(void)
{
	sysGetExecutablePath(exeDir, FS_MAXPATH);

#ifdef __ANDROID__
	// On Android all game data lives in the APK's asset directory under
	// "data/" (mirroring the desktop layout of <exe>/data). baseDir is set to
	// "data" so fsFullPath() produces asset-relative paths such as
	// "data/pd.ntsc-final.z64", which SDL2's Android asset backend resolves
	// against the APK.
	baseDir[0] = '\0';
	strcpy(baseDir, DEFAULT_BASEDIR_NAME);
	// Saves go to the app's internal storage directory.
	const char *sd = SDL_AndroidGetInternalStoragePath();
	if (sd && *sd) {
		strncpy(saveDir, sd, FS_MAXPATH);
	} else {
		sysGetHomePath(saveDir, FS_MAXPATH);
	}
	saveDir[FS_MAXPATH] = '\0';
	strncpy(homeDir, saveDir, FS_MAXPATH);
	homeDir[FS_MAXPATH] = '\0';
	sysLogPrintf(LOG_NOTE, "base dir: %s", baseDir);
	sysLogPrintf(LOG_NOTE, "save dir: %s", saveDir);
	return 0;
#endif

	// if this is set, default to exe path for everything
	const s32 portable = sysArgCheck("--portable");
	if (portable) {
		strcpy(homeDir, exeDir);
	} else {
		sysGetHomePath(homeDir, FS_MAXPATH);
	}

	// get path to base dir and expand it if needed
	const char *path = sysArgGetString("--basedir");
	if (!path) {
		// check if there's a `data` directory in working directory or homeDir, otherwise default to exe directory
		path = "$E/" DEFAULT_BASEDIR_NAME;
		if (!portable) {
			if (fsFileSize("./" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "./" DEFAULT_BASEDIR_NAME;
			} else if (fsFileSize("$H/" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "$H/" DEFAULT_BASEDIR_NAME;
			}
		}
	}
	strncpy(baseDir, fsFullPath(path), FS_MAXPATH);

	// get path to mod dir and expand it if needed
	// mod directory is overlaid on top of base directory
	path = sysArgGetString("--moddir");
	if (path) {
		if (fsPathIsAbsolute(path) || fsPathIsCwdRelative(path) || path[0] == '$') {
			// path is explicit; check as-is
			if (fsFileSize(path) >= 0) {
				strncpy(modDir, fsFullPath(path), FS_MAXPATH);
			}
		} else {
			// path is relative to workdir; try to find it
			const char *priority[] = { ".", "$E", "$H" };
			for (s32 i = 0; i < 2 + (portable != 0); ++i) {
				char *tmp = strFmt("%s/%s", priority[i], path);
				if (fsFileSize(tmp) >= 0) {
					strncpy(modDir, fsFullPath(tmp), FS_MAXPATH);
					break;
				}
			}
		}
		if (!modDir[0]) {
			sysLogPrintf(LOG_WARNING, "could not find specified moddir `%s`", path);
		}
	}

	// get path to save dir and expand it if needed
	path = sysArgGetString("--savedir");
	if (!path) {
		if (portable) {
			path = "$E";
		} else {
#if defined(PLATFORM_LINUX) || defined(PLATFORM_OSX)
			// check if there's a config in the working directory, otherwise default to homeDir
			if (fsFileSize("./" CONFIG_FNAME) >= 0) {
				path = ".";
			} else {
				path = "$H";
			}
#else
			// check if working directory is writable, otherwise default to homeDir
			if (fsPathIsWritable("./")) {
				path = ".";
			} else {
				sysLogPrintf(LOG_WARNING, "cannot write to working directory, will use %s for saves instead", homeDir);
				path = "$H";
			}
#endif
		}
	}

	strncpy(saveDir, fsFullPath(path), FS_MAXPATH);

	if (modDir[0]) {
		sysLogPrintf(LOG_NOTE, " mod dir: %s", modDir);
	}
	sysLogPrintf(LOG_NOTE, "base dir: %s", baseDir);
	sysLogPrintf(LOG_NOTE, "save dir: %s", saveDir);

	return 0;
}

const char *fsGetModDir(void)
{
	return modDir[0] ? modDir : NULL;
}

s32 fsFileLoadTo(const char *name, void *dst, u32 dstSize)
{
	const char *fullName = fsFullPath(name);

	FILE *f = fopen(fullName, "rb");
	if (!f) {
#ifdef __ANDROID__
		return fsAssetLoad(fullName, dst, dstSize);
#else
		return -1;
#endif
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return -1;
	}

	if ((u32)size > dstSize) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: file too big for buffer (%u > %u): %s", size, dstSize, fullName);
		fclose(f);
		return -1;
	}

	fread(dst, 1, size, f);
	fclose(f);

	return size;
}

void *fsFileLoad(const char *name, u32 *outSize)
{
	const char *fullName = fsFullPath(name);

	FILE *f = fopen(fullName, "rb");
	if (!f) {
#ifdef __ANDROID__
		s32 size = fsAssetSize(fullName);
		if (size < 0) {
			sysLogPrintf(LOG_ERROR, "fsFileLoad: could not find file: %s", fullName);
			return NULL;
		}
		void *buf = sysMemZeroAlloc(size + 1);
		if (!buf) {
			sysLogPrintf(LOG_ERROR, "fsFileLoad: could not alloc %d bytes for file: %s", size, fullName);
			return NULL;
		}
		s32 read = fsAssetLoad(fullName, buf, size);
		if (read < 0) {
			sysMemFree(buf);
			sysLogPrintf(LOG_ERROR, "fsFileLoad: could not read file: %s", fullName);
			return NULL;
		}
		if (outSize) {
			*outSize = (u32)read;
		}
		return buf;
#else
		sysLogPrintf(LOG_ERROR, "fsFileLoad: could not find file: %s", fullName);
		return NULL;
#endif
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoad: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return NULL;
	}

	void *buf = NULL;
	if (size) {
		buf = sysMemZeroAlloc(size + 1); // sick hack for a free null terminator
		if (!buf) {
			sysLogPrintf(LOG_ERROR, "fsFileLoad: could not alloc %d bytes for file: %s", size, fullName);
			fclose(f);
			return NULL;
		}
		fread(buf, 1, size, f);
	}

	fclose(f);

	if (outSize) {
		*outSize = size;
	}

	return buf;
}

s32 fsFileSize(const char *name)
{
	const char *fullName = fsFullPath(name);
	struct stat st;
	if (stat(fullName, &st) < 0) {
#ifdef __ANDROID__
		return fsAssetSize(fullName);
#else
		return -1;
#endif
	} else {
		return st.st_size;
	}
}

FILE *fsFileOpenWrite(const char *name)
{
	return fopen(fsFullPath(name), "wb");
}

FILE *fsFileOpenRead(const char *name)
{
	return fopen(fsFullPath(name), "rb");
}

void fsFileFree(FILE *f)
{
	fclose(f);
}

s32 fsCreateDir(const char *path)
{
#ifdef PLATFORM_WIN32
	return _mkdir(fsFullPath(path));
#else
	return mkdir(fsFullPath(path), 0777);
#endif
}
