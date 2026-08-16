@echo off
chcp 65001 >nul
cd /d "D:\Documents\Projects\Project_landwar\new_project_landwar"
if not exist "build\landwar.exe" goto missing
build\landwar.exe
exit /b 0
:missing
echo [ERROR] build\landwar.exe not found. Please build first, see README.md.
exit /b 1
