# ProGuard rules for the Perfect Dark Android port.
# The native library is not obfuscated; keep JNI entry points used by SDLActivity.
-keep class org.sdl2.perfectdark.SDLActivity { *; }
