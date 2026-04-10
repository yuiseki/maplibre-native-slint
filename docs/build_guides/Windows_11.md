````markdown
# Building on Windows 11

This guide walks you through building the canonical C++ reference application for `maplibre-native-slint` on Windows 11 with MSVC, CMake, Ninja, and vcpkg (manifest mode).

---

## System Requirements

- Windows 11 (x64)
- **Visual Studio 2022** (Desktop development with C++) / MSVC toolchain
- **CMake** (3.24+ recommended)
- **Ninja** (1.11+)
- **Git**
- **Rust** (stable) via rustup
- **vcpkg** (manifest mode; the project contains `vcpkg.json`)
- **LLVM** (required when using the WebGPU/wgpu backend — see below)

> Default rendering backend: **WebGPU (`wgpu-native`)**. OpenGL remains available as a fallback/comparison path.

---

## 1) Install prerequisites

1. **Visual Studio 2022**

   - Install _Desktop development with C++_ workload.

2. **CMake / Ninja / Git**

   - Install from official installers or your preferred package manager.
   - **Important**: Enable long paths support in Git to handle deep directory structures in submodules:
     ```powershell
     git config --global core.longpaths true
     ```

3. **Rust (required by Slint)**

   - Install with rustup (the standard Rust toolchain manager on Windows).

4. **vcpkg**
   - Clone to a convenient path (e.g. `C:\src\vcpkg`) and bootstrap.

5. **LLVM** _(required for the WebGPU/wgpu backend only)_
   - Download the Windows installer from https://releases.llvm.org/
   - During installation, select **"Add LLVM to the system PATH"**.
   - If the installer warns that PATH is too long and cannot be modified, that is OK —
     CMake will locate `libclang.dll` automatically from common install paths
     (`C:\Program Files\LLVM\lib` or `bin`).

> Tip: It's easiest to build from the **"x64 Native Tools Command Prompt for VS 2022"** so MSVC env vars are set.
> Note: VSCode integrated terminal does not work as expected...

---

## 2) Clone the repository

```bat
git clone https://github.com/maplibre/maplibre-native-slint.git
cd maplibre-native-slint
git submodule update --init --recursive
```

---

## 3) Configure vcpkg environment variables

```bat
:: Disable VS bundled vcpkg for this session only (to avoid warnings)
set VCPKG_ROOT=
:: Set overlay triplets to match the project (important)
set VCPKG_OVERLAY_TRIPLETS=%cd%\vendor\maplibre-native\platform\windows\vendor\vcpkg-custom-triplets
```

## 4) Configure with CMake (vcpkg manifest mode)

> The project uses **vcpkg manifest mode**. You **don't** need to pass package names to `vcpkg install`; CMake will drive vcpkg using `vcpkg.json`.

### OpenGL fallback

```bat
cmake -S . -B build-ninja -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/src/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DVCPKG_HOST_TRIPLET=x64-windows
```

### WebGPU (wgpu-native) backend

```bat
cmake -S . -B build-ninja -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/src/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DVCPKG_HOST_TRIPLET=x64-windows ^
  -DMLN_WITH_WEBGPU=ON ^
  -DMLN_WEBGPU_IMPL_WGPU=ON ^
  -DPYTHON_EXECUTABLE=C:/Python313/python.exe
```

> Adjust `-DPYTHON_EXECUTABLE` to your actual Python 3 installation path if needed.

On first configure, vcpkg will install the dependencies declared in `vcpkg.json`.
Slint (C++ API) is **auto-fetched/built** by CMake if not found system-wide.

> **Note (WebGPU):** CMake will automatically build `wgpu-native` via Cargo during the configure step.
> This requires LLVM (for `bindgen`). If CMake cannot find `libclang.dll` automatically,
> set the environment variable before running CMake:
> ```bat
> set LIBCLANG_PATH=C:\Program Files\LLVM\lib
> ```
> If the automatic build fails, you can pre-build wgpu-native manually and re-run CMake:
> ```bat
> set LIBCLANG_PATH=C:\Program Files\LLVM\lib
> cd vendor\maplibre-native\vendor\wgpu-native
> cargo build --release
> cd ..\..\..\..
> ```

---

## 5) Build

```bat
cmake --build build-ninja -j
```

---

## 6) Run the Reference Application

The example app and DLLs are placed in the build directory:

```bat
cd build-ninja\cpp
maplibre-slint-example.exe
```

---

## 7) Expected build outputs

- `build-ninja\cpp\maplibre-slint-example.exe` — Example application
- `build-ninja\cpp\slint_cpp.dll` — Slint runtime
- `build-ninja\cpp\cpr.dll` — HTTP client library
- `build-ninja\cpp\wgpu_native.dll` — wgpu-native runtime _(WebGPU backend only, copied automatically)_

---

## Notes

- **OpenGL headers** are provided by vcpkg (header-only _opengl-registry_, _egl-registry_).
- MSVC requires defining `_USE_MATH_DEFINES` before including `<cmath>` to get constants like `M_PI`. This project's build already defines it, but it's good to know.

---

## Troubleshooting

### "In manifest mode, `vcpkg install` does not support individual package arguments"

- Cause: running `vcpkg install zlib ...` in a manifest project.
- Fix: don't pass package names. Let CMake drive vcpkg, or run `vcpkg install` **without arguments** in the repo root (it will read `vcpkg.json`).

### Unresolved externals for `uv_*` (libuv)

- Ensure your app target links **libuv**. If you customize or add targets, link the vcpkg CMake target for libuv, e.g.:

  ```cmake
  find_package(libuv CONFIG REQUIRED)
  target_link_libraries(your-target PRIVATE
      $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
  ```

  Check the vcpkg **usage** file under `vcpkg_installed/x64-windows/share/libuv/usage` for the exact target names exported by your version.

### Missing OpenGL / GLES headers

- Make sure your `vcpkg.json` includes (or your install contains) header-only ports:

  - `opengl-registry` (provides `GL/glcorearb.h`)
  - `egl-registry` (provides `EGL/egl.h`)

### Slint not found

- The build system will **auto-fetch** Slint if it's not installed system-wide. If you prefer system-wide installs, follow Slint's C++ docs and ensure `slint-cpp` is visible via CMake/PKG.

### WebGPU: `Unable to find libclang` during cargo build

- Cause: `bindgen` (used by wgpu-native) requires `libclang.dll` from LLVM.
- Fix: Install LLVM from https://releases.llvm.org/ and set `LIBCLANG_PATH` to the directory containing `libclang.dll` (typically `C:\Program Files\LLVM\lib`).

### WebGPU: `wgpu_native.dll` not found at runtime

- The DLL is automatically copied to the output directory during the CMake build.
  If it is missing, copy it manually:
  ```bat
  copy vendor\maplibre-native\vendor\wgpu-native\target\release\wgpu_native.dll build-ninja\cpp\
  ```

---

## Performance tips

- First build can take a while (MapLibre Native + Slint + optionally wgpu-native).
- Using Ninja and a recent MSVC significantly speeds up incremental builds.
- SSD and more RAM help with link times.

---

## Next steps

1. Start the sample: `maplibre-slint-example.exe`
2. Read `src/maplibre.slint` for the reusable Slint surface
3. Read `cpp/map_window.slint` for the demo shell
4. Read `cpp/main.cpp` for the canonical backend wiring
````
