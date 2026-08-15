@echo off
chcp 65001 >nul
rem 领土战争 — 双击运行（data/ 按当前目录解析，故先 cd 到项目根）
cd /d "D:\Documents\Projects\Project_landwar\new_project_landwar"
if not exist "build\landwar.exe" (
  echo 未找到 build\landwar.exe，请先运行 CMake 构建（见 构建命令.bat / README.md）。
  pause
  exit /b 1
)
build\landwar.exe
