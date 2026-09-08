@echo off
setlocal
pushd "%~dp0"

set "GAME_EXE="
if exist "build-release\landwar.exe" set "GAME_EXE=build-release\landwar.exe"
if not defined GAME_EXE if exist "landwar.exe" set "GAME_EXE=landwar.exe"
if not defined GAME_EXE goto missing

"%GAME_EXE%" %*
set "EXIT_CODE=%errorlevel%"
popd
if not "%EXIT_CODE%"=="0" echo [ERROR] Game exited with code %EXIT_CODE%.
pause
endlocal & exit /b %EXIT_CODE%

:missing
echo [ERROR] landwar.exe was not found.
echo [INFO] Run the build script first, or place landwar.exe beside this file.
popd
pause
endlocal & exit /b 1
