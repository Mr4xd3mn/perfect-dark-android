# Perfect Dark on Android

An Android build of the port. The game code is the same decompilation the
desktop builds use; the Android-specific work is the platform layer, the
on-screen controls and the Gradle project.

The full instructions for building are in this file. A shorter guide to
installing and playing the game is in [PLAY_ON_ANDROID.md](PLAY_ON_ANDROID.md).

## Status

The following has been added on top of the desktop port:

* on-screen controls: a floating analog stick for movement, a relative drag
  area for camera look, and an N64-style button pad;
* mouselook aiming, driven by the on-screen look area;
* menu controls: a direction pad, a confirm button and a back button, shown
  instead of the gamepad while a menu is open;
* immersive fullscreen, with the orientation locked to landscape;
* options for the on-screen controls, stored in `pd.ini`.

The following configurations are supported and tested:

* Android 7.0 (API 24) and newer;
* `arm64-v8a` and `armeabi-v7a`;
* OpenGL ES 2.0 or newer;
* OpenSL ES for audio.

The build produces a debug APK. Release builds are not signed, so they have
to be signed before they can be installed.

## The ROM

The ROM is copyrighted, so it is not included in the APK or in the repository,
and it is never downloaded. The app asks for it on the first run, copies it
into the app's internal storage, and loads it from there.

The file picker stays open for as long as the player wants it to. Cancelling
it does not end anything: the app offers to open it again. The only ways out
are the *Exit* button and the system back button.

The ROM this build expects is `ntsc-final` (US v1.1, md5
`e03b088b6ac9e0080440efed07c1e40f`). `ntsc-1.0` also works. PAL and JPN need
a build with a different `-DROMID`.

## Building

Building does not need the ROM. The game reads its data from the ROM at
runtime, and the build only needs the JSON asset headers that are already in
the repository.

### 1. Install the toolchain

`tools/setup/setup.py` downloads CMake, Ninja, a JDK, the Android SDK and
NDK, and cross-compiles SDL2 for both architectures:

```
python3 android/tools/setup/setup.py
```

It writes an environment file that sets the variables the build needs:

```
source ../build-deps/env.sh
```

The toolchain goes to `../build-deps` by default. Pass `--deps <dir>` to put
it somewhere else.

`--check-urls` only checks that the downloads are still available, which is
useful when one of them has moved:

```
python3 android/tools/setup/setup.py --check-urls
```

SDL2 has to be built as a shared library, because the Android activity loads
it by name. The port itself is built as `libmain.so`.

### 2. Build the native library

```
for ABI in arm64-v8a armeabi-v7a; do
  cmake -S . -B "build-$ABI" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_static \
    -DROMID=ntsc-final \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL2_INCLUDE_DIR="$PWD/../build-deps/sdl2-android/$ABI/include/SDL2" \
    -DSDL2_LIBRARY="$PWD/../build-deps/sdl2-android/$ABI/lib/libSDL2.so" \
    -G Ninja
  cmake --build "build-$ABI" -j"$(nproc)"
done
```

The C++ runtime is linked statically, so the APK does not have to ship
`libc++_shared.so`.

### 3. Collect the libraries

```
for ABI in arm64-v8a armeabi-v7a; do
  mkdir -p "android/app/src/main/jniLibs/$ABI"
  cp "build-$ABI/android/libs/$ABI/libmain.so" "android/app/src/main/jniLibs/$ABI/"
  cp "../build-deps/sdl2-android/$ABI/lib/libSDL2.so" "android/app/src/main/jniLibs/$ABI/"
done
```

### 4. Build the APK

```
cd android
./gradlew assembleDebug
```

`gradlew` is the standard Gradle wrapper, so it downloads Gradle itself. It
needs a JDK, which the environment file from step 1 provides.

The resulting APK will be at
`android/app/build/outputs/apk/debug/app-debug.apk`.

### 5. Install

```
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The APK can also be copied to the device and opened there.

## Controls

The game runs in landscape.

### During gameplay

| Control | Where | Action |
|---|---|---|
| Analog stick | anywhere in the left half | Walk and strafe. The stick appears under the finger. |
| Look | drag anywhere in the right half | Turn the camera. Relative, so there is no limit to how far the camera can turn in one gesture. |
| **Z** | red | Fire |
| **A** | green | Action, or open |
| **B** | blue | Previous or next weapon |
| **R** | yellow | Aim mode |
| **START** | white, top right | Pause menu |
| **L** | yellow, top left | Alt fire |

A drag never fires the gun. A finger that goes down on a button and then
moves is treated as a drag rather than a press, and the drag turns the
camera instead.

### In menus

The gamepad is hidden while a menu is open, and a direction pad, **OK** and
**BACK** are shown instead. Dragging on the left half moves the selection and
tapping anywhere else confirms it. The system back button goes back one level.

### Gamepads

A Bluetooth or USB gamepad works as well, and can be used at the same time as
the on-screen controls.

## Options

`Options` → `Extended Options` → `Touch` holds the settings for the on-screen
controls: aim sensitivity, stick size and deadzone, overlay opacity, button
layout, haptics, and whether the touch zones are drawn. They are saved to
`pd.ini`.

The `Mouse` options page affects the on-screen look area as well, because the
touch look is fed into the port's mouselook code.

## Implementation notes

* SDL2's `org.libsdl.app.SDLActivity` is vendored under
  `app/src/main/java/org/libsdl/app/`. It loads `libSDL2.so` and
  `libmain.so`, gives SDL the JavaVM and the Android context, creates the
  surface, and starts the thread that calls the port's `SDL_main`.
* `port/src/android_rom.c` asks the activity to show the system file picker
  through JNI and waits for the result. The picked file is copied into the
  app's internal storage, where `port/src/romdata.c` looks for it.
* Touch input arrives as `SDL_FINGER*` events and is turned into the port's
  own `OSContPad` state in `port/src/touch.c`. Pointers are tracked by touch
  device id and finger id together, and each pointer keeps one role - move,
  look or button - for as long as it is down.
* The on-screen controls are written straight into the `OSContPad`, the same
  path the keyboard bindings use. A virtual SDL gamepad is deliberately not
  used: the port's default bindings already map gamepad buttons onto their
  own meanings, so routing the buttons through one would deliver the wrong
  actions.
* SDL's synthetic touch-to-mouse events are switched off with
  `SDL_HINT_TOUCH_MOUSE_EVENTS`. By default SDL turns every touch into a
  mouse click and mouse movement, and since the port binds fire to the left
  mouse button, that would make every touch fire the gun and turn the camera.
* The overlay is drawn with a small GLES2 shader just before each frame is
  presented. It is laid out in the size of the framebuffer rather than the
  size of the surface, because some drivers hand out a framebuffer that is
  smaller than the surface and clamp the viewport to it.
* All Android-specific C code is guarded with `#ifdef __ANDROID__`, so the
  desktop builds are unaffected.

## The launcher icon

The icons in `app/src/main/res/mipmap-*` are generated from a piece of
artwork by `tools/icons/make_icons.py`:

```
python3 android/tools/icons/make_icons.py <artwork.jpg> android/app/src/main/res
```

It writes a square icon and a round one for older launchers, and an adaptive
icon for API 26 and newer, with the artwork as the background and the
wordmark as the foreground, so that the launcher's mask cannot cut the
wordmark up.

The artwork the current icons were made from is the Perfect Dark Game Boy
Color box cover, which belongs to Nintendo. It is not in the repository, and
the icons should be replaced with original artwork before the project is
published.

## Credits

* the original [decompilation project](https://github.com/n64decomp/perfect_dark) authors;
* Ryan Dwyer and the [PC port](https://github.com/perfect-dark-pc-port/perfect_dark);
* the [SDL2](https://github.com/libsdl-org/SDL) project, for the Android
  platform layer, the Java classes under `app/src/main/java/org/libsdl/app/`
  and the GLES2 backend;
* everyone who has submitted pull requests and issues to the PC port.

Perfect Dark is a trademark of Nintendo. This project contains no Nintendo
code or assets, and the game ROM is neither included nor downloaded.
