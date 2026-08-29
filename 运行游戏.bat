@echo off
chcp 65001 >nul
cd /d "D:\Documents\Projects\Project_landwar\new_project_landwar"
if not exist "build-release\landwar.exe" goto missing
build-release\landwar.exe
exit /b 0
:missing
echo [ERROR] build-release\landwar.exe not found. Please build first, see README.md.
exit /b 1
