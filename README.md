
# landwar：领土战争

`landwar` 是一个使用AI开发, 使用了 C++20、SDL2、EnTT 和 Dear ImGui 构建的简易实时领土战争模拟游戏。
多个可配置 AI 势力在地图上持续扩张和交战。
玩家可选择一个势力进行游玩。

## 项目特点

- 固定步长模拟与确定性随机数：指定相同的地图种子、主种子、配置和选项即可复现同一局面。
- 配置驱动的势力、兵种、科技、经济、地图和渲染参数。
- 正方形、六边形、三角形，以及半正和 Laves 密铺地图（排除两种过于简单无聊的），共17种。
- 山地、河流、陆地、海洋、城镇、多种地形。
- 兵无法被玩家控制，而是自动游走战斗。
- 主菜单选势力和地图、随机地图预览、玩家模式、科技选择、消息面板和截图功能。
- 支持无头运行、确定性摘要、存档/读档和回放，方便测试与实验。

## 快速开始

Windows 开发构建使用 MSYS2 UCRT64：

```bash
cmake --preset default
cmake --build --preset release
ctest --preset release --output-on-failure
build-release/landwar.exe
```

确定性无头烟测：

```bash
build-release/landwar.exe --headless --seed 42 --ticks 1000 --summary
```

WSL / Linux（Ubuntu、Debian）构建与测试：

```bash
sudo apt-get install -y cmake ninja-build g++ pkg-config \
    libsdl2-dev libsdl2-image-dev nlohmann-json3-dev libspdlog-dev libgtest-dev
cmake --preset linux
cmake --build --preset linux
ctest --preset linux --output-on-failure
```

> 首次构建需在 `_deps/` 准备锁定版本的 Dear ImGui 与 EnTT（与 CI 相同）。
> 完整步骤、依赖清单与 WSL 注意事项见 [`运行说明.md`](运行说明.md) 的「WSL / Linux 本地构建」。

Linux 构建和测试同样由 GitHub Actions 自动执行。完整依赖、版本和许可证说明见
[`THIRD_PARTY.md`](THIRD_PARTY.md)。

## 配置与扩展

`data/` 中的 JSONC / CSV 文件可以调整游戏规则。兵种、势力、科技采用 **CSV+JSONC 混合**：
必填单值列在 `data/units.csv`、`data/factions.csv`、`data/techs.csv`，独有/可选字段在对应的
`.jsonc` 覆盖层（按 `type`/`id` 合并）。可以在 `data/factions.csv` 追加连续 ID 的势力
（可选字段写进 `data/factions.jsonc`）。

ID 0 保留给中立势力，ID 必须从 0 开始连续。修改后运行：

```bash
build-release/landwar.exe --validate-config
```

## 当前状态

这是一个持续完善中的项目。快照版本采用严格匹配策略，跨版本读档不保证兼容；窗口模式默认使用。
Windows 系统中文字体，发行包不捆绑字体。运行时需要从项目或发行包根目录启动，并保证 `userdata/`
可写。版本更新记录见 [`CHANGELOG.md`](CHANGELOG.md)。

## 参与开发

暂时不是很会用这边的功能。