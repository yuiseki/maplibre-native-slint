@echo off
:: Pre-builds wgpu-native from source when CMake cannot invoke cargo automatically.
:: Required when LIBCLANG_PATH is not inherited by CMake's execute_process on Windows.
:: Run this before build_windows.bat if configure fails with a libclang error.

set LIBCLANG_PATH=C:\Program Files\LLVM\lib

cd /d "%~dp0vendor\maplibre-native\vendor\wgpu-native"

cargo build --release

if errorlevel 1 (
    echo ERROR: wgpu-native cargo build failed.
    echo Make sure LLVM is installed and libclang.dll exists in:
    echo   %LIBCLANG_PATH%
    exit /b 1
)

echo.
echo wgpu-native build SUCCESS
echo You can now run build_windows.bat
