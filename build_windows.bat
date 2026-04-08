@echo off
:: Build helper for Windows 11 with WebGPU (wgpu-native) backend.
:: Run from the repository root in a plain Command Prompt or PowerShell.
:: Requires: Visual Studio 2022, CMake, Ninja, Rust, vcpkg at C:\src\vcpkg, LLVM.

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed - is Visual Studio 2022 installed?
    exit /b 1
)

set VCPKG_ROOT=
set VCPKG_OVERLAY_TRIPLETS=%~dp0vendor\maplibre-native\platform\windows\vendor\vcpkg-custom-triplets
set LIBCLANG_PATH=C:\Program Files\LLVM\lib

cmake -S "%~dp0" ^
  -B "%~dp0build-ninja" ^
  -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/src/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DVCPKG_HOST_TRIPLET=x64-windows ^
  -DMLN_WITH_WEBGPU=ON ^
  -DMLN_WEBGPU_IMPL_WGPU=ON ^
  -DPYTHON_EXECUTABLE=C:/Python313/python.exe

if errorlevel 1 (
    echo.
    echo ERROR: cmake configure failed.
    echo If the error is about libclang, pre-build wgpu-native first:
    echo   build_wgpu_native.bat
    exit /b 1
)

cmake --build "%~dp0build-ninja" -j

if errorlevel 1 (
    echo ERROR: cmake build failed
    exit /b 1
)

echo.
echo BUILD SUCCESS
echo Run: build-ninja\cpp\maplibre-slint-example.exe
