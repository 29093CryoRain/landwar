@echo off
rem 构建命令速查（README.md §构建；本文件即文档，双击无操作）
rem
rem 配置 / 编译 / 测试（Ninja Debug，生成 build/）：
rem   cmake --preset default
rem   cmake --build --preset default
rem   ctest --preset default
rem
rem 发布预设（2026-08 工程改进新增；ASan/UBSan 未内置——本机工具链缺 libasan/libubsan）：
rem   cmake --preset release && cmake --build --preset release    :: build-release/
rem
rem 无头确定性基线（20000 tick 摘要 state_hash，应与 tests/test_baseline.cpp 期望一致）：
rem   build\landwar.exe --headless --seed 42 --ticks 20000 --summary
rem
rem 运行（须从项目根；运行期产物写 userdata/，资产只读 data/）：
rem   build\landwar.exe
echo 命令速查见本文件注释（README.md 亦有）。

pause
