# Playing Perfect Dark on Android

A guide to installing the game and playing it. For how to build the APK, see
[README.md](README.md).

## Getting the ROM

The ROM is copyrighted, so it is not included in the APK or in the
repository, and it is never downloaded for you. A copy of the NTSC v1.1 ROM
is needed:

| ROM | File name | MD5 |
|---|---|---|
| `ntsc-final` / US V1.1 / US Rev 1 | `pd.ntsc-final.z64` | `e03b088b6ac9e0080440efed07c1e40f` |

It is sold as part of *Perfect Dark* on Nintendo 64, and the
[Internet Archive](https://archive.org/) has it as part of its preservation
efforts. The file cannot be linked from here, so it has to be looked up
separately.

## Installing

Copy `app-debug.apk` to the device and open it, allowing "install from
unknown sources" when Android asks. It can also be installed with adb:

```
adb install -r app-debug.apk
```

Requirements: Android 7.0 (API 24) or newer, and an `arm64-v8a` or
`armeabi-v7a` device.

### The first run

The first time the game starts it asks for the ROM:

* the system file picker opens;
* pick `pd.ntsc-final.z64` - the name does not matter, the app renames it;
* the file is copied into the app's own storage, and the game starts.

The file picker stays open for as long as the player wants it to. Cancelling
it does not end anything: the app asks whether the file should be picked
again, and the only ways out are the *Exit* button and the system back
button.

The ROM is only asked for once. After that the game goes straight to the
title screen. If the copy fails - the file was too small, or the wrong file
was picked - the app asks again on the next start.

## Controls

The game runs in landscape in full immersive mode, so the status bar and the
navigation bar are hidden until they are swiped into view. The on-screen
controls are drawn over the game.

The port runs in its "PC" control style, which means the second stick moves
the player and the mouse drives the camera. The on-screen look area feeds
that same mouse code, so aiming behaves the way it does on the desktop.

### During gameplay

**Left thumb - movement.** Touch anywhere in the left half of the screen. The
stick appears under the finger and follows it; release to stop. Up and down
walks, left and right strafes. It is bound to movement only and never turns
the camera.

**Right thumb - looking.** Touch and drag anywhere in the right half that is
not a button. The movement is relative, so the camera turns by the same
amount regardless of where the thumb is, and there is no limit to how far it
can turn in one gesture.

**Dragging never fires the gun.** A finger that goes down on a button and
then moves is treated as a drag rather than a press, so looking around cannot
shoot by accident.

**Buttons.** An N64-style cluster in the bottom right:

| Button | Colour | Action |
|---|---|---|
| **Z** | red | Fire |
| **A** | green | Action, or open |
| **B** | blue | Previous or next weapon |
| **R** | yellow | Aim mode |
| **START** | white, top right | Pause menu |
| **L** | yellow, top left | Alt fire |

Buttons are held while they are touched, and light up when they are pressed.
The layout can be changed to *Arc* or *Minimal* in the options.

### In menus

The gamepad is hidden while a menu, the pause screen or a cutscene is
showing, so it does not cover the interface. A set of navigation controls
appears in its place:

* a direction pad in the bottom left, for moving the selection;
* **OK** in the bottom right, for confirming the selected item;
* **BACK** next to it, for going back one level.

The movement stick is not drawn in menus, so it does not get in the way. The
menu can also be operated directly: dragging on the left half moves the
selection, and tapping anywhere else confirms it.

### The system back button

The back button is mapped onto the game's own back action, which is Escape.
It closes menus, and opens the pause menu during gameplay. If nothing acts
on it, the app closes.

### Gamepads

The original desktop bindings still work with a Bluetooth or USB gamepad, and
a physical pad and the on-screen controls can be used at the same time.

### Options

`Options` → `Extended Options` → `Touch` holds:

* Touch Controls - on or off;
* Show Gamepad - draw the button cluster;
* Layout - Gamepad, Arc or Minimal;
* Haptics - vibrate when a button is pressed;
* Show Touch Zones - draw the stick areas;
* Overlay Opacity, Stick Size, Stick Deadzone;
* Aim Sensitivity - how fast the camera turns, 0 to 600%;
* Button Size - scales every on-screen button, 60 to 160%;
* Edit Button Layout - opens the layout editor;
* Reset Button Layout - puts the buttons back where they started;
* Look Sensitivity X and Y - fine tuning one direction at a time.

The settings are saved to `pd.ini` and persist.

The `Mouse` options page still applies, and its mouse speed settings also
affect how fast the on-screen look area turns the camera.

### Layout editor

`Edit Button Layout` opens an editor over the game:

* drag a button to move it;
* **DONE** at the top centre leaves the editor;
* **RESET** next to it puts every button back.

The buttons are not passed to the game while the editor is open, so moving
them cannot fire anything. The positions are saved to `pd.ini` like every
other setting.

## Troubleshooting

### The app crashes on launch

```
adb logcat -c
adb logcat -s SDL:V AndroidRuntime:E
```

| Log line | Meaning |
|---|---|
| `SDL Error: ...` | SDL2 failed to start, usually a missing `libSDL2.so` |
| `UnsatisfiedLinkError` | A native library is missing from `jniLibs` |
| `libc++_shared.so not found` | The C++ runtime was not linked statically; rebuild with `-DANDROID_STL=c++_static` |
| `Could not open ROM file` | The ROM is missing, or was not copied correctly |

### The game asks for the ROM every time

The ROM is copied into the app's internal storage on the first run. If the
copy failed, it is asked for again. Make sure the real 32 MB `ntsc-final`
ROM is picked, and not an archive or a ROM with a different byte order.

### Black screen

* Check the device has an OpenGL ES 2.0 or newer GPU.
* Check the ROM was accepted; a wrong ROM makes the game exit with a message
  instead of starting.

### The touch controls are in the wrong place, or the wrong size

* `Button Size` in the touch options scales every button.
* `Edit Button Layout` opens an editor where the buttons can be dragged.
* `Reset Button Layout` puts them back.
* To report a layout problem, `adb logcat -s SDL:V | grep "touch:"` prints the
  surface and framebuffer sizes. They should be the same; if they are not,
  the driver gave the app a smaller framebuffer than the screen has.

### Uninstalling

```
adb uninstall org.sdl2.perfectdark
```

## How the port works on Android

* `port/src/main.c` defines `main()`, which `SDL_main.h` renames to
  `SDL_main` on Android.
* SDL2's `org.libsdl.app.SDLActivity` is vendored under
  `app/src/main/java/org/libsdl/app/`. It loads `libSDL2.so` and
  `libmain.so`, gives SDL the JavaVM and the Android context, creates the
  surface, and starts the thread that calls the port's `SDL_main`.
* The port's `videoInit()` then creates the SDL window and the GLES2 context
  with the usual `SDL_CreateWindow` and `SDL_GL_CreateContext` calls. It is
  the same code path as on the desktop, because SDL's Android video driver
  backs the window with the Java surface.
* `port/src/android_rom.c` asks the activity to show the system file picker
  through JNI and waits for the result. The picked file is copied into the
  app's internal storage, where `port/src/romdata.c` looks for it.
* Touch input arrives as `SDL_FINGER*` events, is caught by the event watch
  in `port/src/input.c`, and is turned into virtual stick and button state in
  `port/src/touch.c`. Pointers are tracked by touch device id and finger id
  together, so two fingers cannot be confused with each other, and each
  pointer keeps one role - move, look or button - for as long as it is down.
* The on-screen controls are written straight into the port's `OSContPad`,
  the same path the keyboard bindings use. A virtual SDL gamepad is
  deliberately not used: the port's default bindings already map gamepad
  buttons onto their own meanings, so routing the buttons through one would
  deliver the wrong actions.
* SDL's synthetic touch-to-mouse events are switched off with
  `SDL_HINT_TOUCH_MOUSE_EVENTS`. By default SDL turns every touch into a
  mouse click and mouse movement, and since the port binds fire to the left
  mouse button, that would make every touch fire the gun and turn the
  camera.
* The overlay is drawn with a small GLES2 shader just before each frame is
  presented.
* `port/src/android_glue.c` holds the OpenGL ES 2.0 stubs that SDL2's loader
  expects but GLES2 does not provide.
