@echo off
chcp 65001 >nul
cd /d "D:\Documents\Projects\Project_landwar\new_project_landwar"
if not exist "build\landwar.exe" (
  echo [ERROR] build\landwar.exe not found. Please build first (see README.md).
  pause
  exit /b 1
)
build\landwar.exe
