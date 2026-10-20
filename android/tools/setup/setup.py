#!/usr/bin/env python3
"""
Set up everything needed to build the Android port.

Downloads CMake, Ninja, a JDK, the Android SDK and NDK, and the SDL2 source,
then cross-compiles SDL2 for arm64-v8a and armeabi-v7a.

    python android/tools/setup/setup.py            # into <repo>/../build-deps
    python android/tools/setup/setup.py --deps /tmp/deps

The script only downloads what is missing, so it can be re-run safely.
"""

import argparse
import os
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

IS_WINDOWS = platform.system() == "Windows"
HOST = platform.machine()  # x86_64 on CI

CMAKE_VERSION = "3.28.1"
NINJA_VERSION = "1.11.1"
JDK_VERSION = "17.0.12_7"
CMDLINE_TOOLS_VERSION = "11076708"
NDK_VERSION = "r25c"
SDL2_VERSION = "2.28.5"

ANDROID_API = "34"
ANDROID_MIN_API = "24"

ABIS = ["arm64-v8a", "armeabi-v7a"]


def log(msg):
    print(f"[setup] {msg}", flush=True)


def download(url, dest):
    """Download a file unless it is already there."""
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        log(f"have {os.path.basename(dest)}")
        return dest
    log(f"downloading {url}")
    tmp = dest + ".part"
    urllib.request.urlretrieve(url, tmp)
    os.replace(tmp, dest)
    return dest


def extract(archive, dest):
    """Unpack a zip or tarball into dest."""
    log(f"extracting {os.path.basename(archive)}")
    if archive.endswith(".zip"):
        extract_zip(archive, dest)
    else:
        extract_tar(archive, dest)


def extract_zip(archive, dest):
    """
    Unpacks a zip, keeping symbolic links and the permission bits.

    The Android NDK for Linux ships its tools as links - bin/clang points at
    bin/clang-14, and there are a lot of them. Python's zipfile does not
    restore links, it writes the target of the link as the content of the
    file instead, and the NDK's compiler then fails with "clang-14: not
    found". The same goes for the permission bits, without which the tools
    are not executable.
    """
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            path = os.path.join(dest, info.filename)
            mode = info.external_attr >> 16

            if stat.S_ISLNK(mode):
                # A link: the target is stored as the content of the entry.
                target = z.read(info).decode("utf-8", "replace")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                if os.path.lexists(path):
                    os.remove(path)
                try:
                    os.symlink(target, path)
                except (OSError, NotImplementedError, AttributeError):
                    # Windows without developer mode: write the target out
                    # as a plain file instead.
                    with open(path, "w") as f:
                        f.write(target)
                continue

            z.extract(info, dest)

            if mode & 0o777 and not IS_WINDOWS:
                # Windows has no executable bit, and setting a file
                # read-only there would only get in the way.
                try:
                    os.chmod(path, mode & 0o777)
                except OSError:
                    pass


def extract_tar(archive, dest):
    """Unpacks a tarball. The permissions are kept as they are."""
    with tarfile.open(archive) as t:
        try:
            t.extractall(dest, filter="data")
        except TypeError:
            # Python older than 3.12 has no filters.
            t.extractall(dest)


def find_subdir(parent, prefix):
    """Return the first directory in parent whose name starts with prefix."""
    for name in sorted(os.listdir(parent)):
        path = os.path.join(parent, name)
        if name.startswith(prefix) and os.path.isdir(path):
            return path
    return None


class Setup:
    def __init__(self, deps):
        self.deps = os.path.abspath(deps)
        self.dl = os.path.join(self.deps, "downloads")
        self.sdk = os.path.join(self.deps, "android-sdk")
        self.ndk = os.path.join(self.deps, "android-ndk", "android-ndk-" + NDK_VERSION)
        self.sdl2_prefix = os.path.join(self.deps, "sdl2-android")

    # --- downloads -----------------------------------------------------

    def url(self, name):
        if name == "cmake":
            plat = "windows-x86_64.zip" if IS_WINDOWS else "linux-x86_64.tar.gz"
            return f"https://github.com/Kitware/CMake/releases/download/v{CMAKE_VERSION}/cmake-{CMAKE_VERSION}-{plat}"
        if name == "ninja":
            plat = "win.zip" if IS_WINDOWS else "linux.zip"
            return f"https://github.com/ninja-build/ninja/releases/download/v{NINJA_VERSION}/ninja-{plat}"
        if name == "jdk":
            plat = "windows" if IS_WINDOWS else "linux"
            ext = "zip" if IS_WINDOWS else "tar.gz"
            # The release is tagged jdk-17.0.12+7, but the asset is named
            # ..._17.0.12_7..., so the two spell the version differently.
            tag = JDK_VERSION.replace("_", "+").replace("+", "%2B")
            return (f"https://github.com/adoptium/temurin17-binaries/releases/download/"
                    f"jdk-{tag}/"
                    f"OpenJDK17U-jdk_x64_{plat}_hotspot_{JDK_VERSION}.{ext}")
        if name == "cmdline-tools":
            plat = "win" if IS_WINDOWS else "linux"
            return (f"https://dl.google.com/android/repository/"
                    f"commandlinetools-{plat}-{CMDLINE_TOOLS_VERSION}_latest.zip")
        if name == "ndk":
            plat = "windows" if IS_WINDOWS else "linux"
            return f"https://dl.google.com/android/repository/android-ndk-{NDK_VERSION}-{plat}.zip"
        if name == "sdl2":
            return (f"https://github.com/libsdl-org/SDL/releases/download/"
                    f"release-{SDL2_VERSION}/SDL2-{SDL2_VERSION}.zip")
        raise ValueError(name)

    def fetch(self, name):
        url = self.url(name)
        ext = ".zip" if url.endswith(".zip") else ".tar.gz"
        dest = os.path.join(self.dl, name + ext)
        download(url, dest)
        return dest

    # --- steps ---------------------------------------------------------

    def step_cmake(self):
        archive = self.fetch("cmake")
        target = os.path.join(self.deps, "cmake")
        if not os.path.isdir(target):
            extract(archive, target)
        sub = find_subdir(target, "cmake-")
        self.cmake_bin = os.path.join(sub, "bin")

    def step_ninja(self):
        archive = self.fetch("ninja")
        target = os.path.join(self.deps, "ninja")
        if not os.path.isdir(target):
            extract(archive, target)
        self.ninja_exe = os.path.join(target, "ninja.exe" if IS_WINDOWS else "ninja")

    def step_jdk(self):
        archive = self.fetch("jdk")
        target = os.path.join(self.deps, "jdk")
        if not os.path.isdir(target):
            extract(archive, target)
        sub = find_subdir(target, "jdk-")
        self.java_home = sub
        self.java_bin = os.path.join(sub, "bin")

    def step_gradle(self):
        """
        Gradle itself is not needed here: the project ships the standard
        Gradle wrapper, which downloads the distribution it needs. The
        wrapper is in android/gradlew and android/gradlew.bat.
        """
        log("skipping gradle - the Gradle wrapper in android/ downloads it")


    def step_sdk(self):
        archive = self.fetch("cmdline-tools")
        if not os.path.isdir(self.sdk):
            os.makedirs(self.sdk, exist_ok=True)
            extract(archive, self.sdk)
            # sdkmanager insists on living in cmdline-tools/latest
            inner = os.path.join(self.sdk, "cmdline-tools")
            latest = os.path.join(inner, "latest")
            if not os.path.isdir(latest) and os.path.isdir(inner):
                os.makedirs(latest, exist_ok=True)
                for name in os.listdir(inner):
                    if name == "latest":
                        continue
                    shutil.move(os.path.join(inner, name), os.path.join(latest, name))

        self.sdkmanager = os.path.join(self.sdk, "cmdline-tools", "latest", "bin",
                                      "sdkmanager.bat" if IS_WINDOWS else "sdkmanager")

    def step_ndk(self):
        archive = self.fetch("ndk")
        parent = os.path.join(self.deps, "android-ndk")
        if not os.path.isdir(self.ndk):
            os.makedirs(parent, exist_ok=True)
            extract(archive, parent)

    def step_sdl2_source(self):
        archive = self.fetch("sdl2")
        target = os.path.join(self.deps, "sdl2-src")
        if not os.path.isdir(target):
            extract(archive, target)
        sub = find_subdir(target, "SDL2-")
        self.sdl2_src = sub

    def env(self):
        env = dict(os.environ)
        env["JAVA_HOME"] = self.java_home
        env["ANDROID_HOME"] = self.sdk
        env["ANDROID_SDK_ROOT"] = self.sdk
        env["ANDROID_NDK_HOME"] = self.ndk
        env["PATH"] = os.pathsep.join([
            self.cmake_bin, self.ninja_bin_dir(), self.java_bin, env.get("PATH", "")])
        return env

    def ninja_bin_dir(self):
        return os.path.dirname(self.ninja_exe)

    def run(self, cmd, **kwargs):
        log(" ".join(str(c) for c in cmd))
        return subprocess.run(cmd, check=True, **kwargs)

    def step_licenses(self):
        env = self.env()
        # sdkmanager asks for every license separately, so it needs a "y" for
        # each of them.
        answers = "y\n" * 200
        for _ in range(20):
            proc = subprocess.run([self.sdkmanager, "--licenses"], env=env,
                                  input=answers, text=True, capture_output=True)
            if "not accepted" not in (proc.stdout + proc.stderr):
                break
        log("licenses accepted")

    def step_sdk_packages(self):
        env = self.env()
        packages = [
            "platform-tools",
            f"platforms;android-{ANDROID_API}",
            f"platforms;android-{ANDROID_MIN_API}",
            f"build-tools;{ANDROID_API}.0.0",
        ]
        answers = "y\n" * 200
        for _ in range(20):
            proc = subprocess.run([self.sdkmanager] + packages, env=env,
                                  input=answers, text=True, capture_output=True)
            if "not accepted" not in (proc.stdout + proc.stderr):
                break
        log("sdk packages installed")

    def step_sdl2(self):
        env = self.env()
        for abi in ABIS:
            build = os.path.join(self.deps, "sdl2-build", abi)
            prefix = os.path.join(self.sdl2_prefix, abi)
            ninja = os.path.join(build, "build.ninja")
            if not os.path.exists(ninja):
                os.makedirs(build, exist_ok=True)
                self.run([
                    os.path.join(self.cmake_bin, "cmake"),
                    "-S", self.sdl2_src, "-B", build,
                    f"-DCMAKE_TOOLCHAIN_FILE={os.path.join(self.ndk, 'build', 'cmake', 'android.toolchain.cmake')}",
                    f"-DANDROID_ABI={abi}",
                    f"-DANDROID_PLATFORM=android-{ANDROID_MIN_API}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DANDROID_STL=c++_static",
                    "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF",
                    "-DSDL_TEST_LIBRARY=OFF",
                    f"-DCMAKE_INSTALL_PREFIX={prefix}",
                    "-G", "Ninja",
                ], env=env)
            self.run([os.path.join(self.cmake_bin, "cmake"), "--build", build,
                      "--target", "install"], env=env)
            log(f"built SDL2 for {abi}")

    def run_all(self):
        self.step_cmake()
        self.step_ninja()
        self.step_jdk()
        self.step_gradle()
        self.step_sdk()
        self.step_ndk()
        self.step_sdl2_source()
        self.step_licenses()
        self.step_sdk_packages()
        self.step_sdl2()

        env_file = os.path.join(self.deps, "env.sh")
        with open(env_file, "w") as f:
            f.write("# source this to set up the environment\n")
            f.write(f'export JAVA_HOME="{self.java_home}"\n')
            f.write(f'export ANDROID_HOME="{self.sdk}"\n')
            f.write(f'export ANDROID_SDK_ROOT="{self.sdk}"\n')
            f.write(f'export ANDROID_NDK_HOME="{self.ndk}"\n')
            f.write(f'export PATH="{self.cmake_bin}:{self.ninja_bin_dir()}:'
                    f'{self.java_bin}:$PATH"\n')
        log(f"done. the environment is written to {env_file}")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    default_deps = os.path.join(os.path.dirname(repo), "build-deps")

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--deps", default=default_deps,
                    help="where to put the downloaded toolchain")
    ap.add_argument("--check-urls", action="store_true",
                    help="only check that the download URLs are still valid")
    args = ap.parse_args()

    setup = Setup(args.deps)

    if args.check_urls:
        bad = 0
        for plat in ("windows", "linux"):
            was = IS_WINDOWS
            globals()["IS_WINDOWS"] = (plat == "windows")
            for name in ("cmake", "ninja", "jdk", "cmdline-tools", "ndk", "sdl2"):
                url = setup.url(name)
                try:
                    req = urllib.request.Request(
                        url, method="HEAD", headers={"User-Agent": "setup.py"})
                    with urllib.request.urlopen(req, timeout=60) as resp:
                        size = int(resp.headers.get("Content-Length", 0))
                        print("OK   %-8s %-14s %8.1f MB" %
                              (plat, name, size / 1e6))
                except Exception as e:
                    bad += 1
                    print("FAIL %-8s %-14s %s" % (plat, name, e))
            globals()["IS_WINDOWS"] = was
        if bad:
            raise SystemExit("%d url(s) are not valid" % bad)
        print("all the download urls are valid")
        return

    setup.run_all()


if __name__ == "__main__":
    main()
