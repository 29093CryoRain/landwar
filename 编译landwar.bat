@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo [1/2] CMake configure (Ninja Release - UCRT64)...
cmake --preset release
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [2/2] CMake build...
cmake --build --preset release
if errorlevel 1 (
    echo [ERROR] CMake build failed.
    exit /b 1
)

if not exist "build-release\landwar.exe" (
    echo [ERROR] build-release\landwar.exe not found after build.
    exit /b 1
)

echo.
echo [OK] build-release\landwar.exe is ready.
exit /b 0
