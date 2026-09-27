# landwar：领土战争

`landwar` 是一个使用 AI 开发、基于 C++20、SDL2、EnTT 和 Dear ImGui 构建的实时领土战争模拟游戏。
多个可配置 AI 势力在地图上持续扩张和交战，玩家可选择一个势力游玩。

- 固定步长模拟与确定性随机数：指定相同的地图种子、主种子、配置和选项即可复现同一局面。
- 配置驱动的势力、兵种、科技、经济、地图和渲染参数。
- 正方形、六边形、三角形，以及半正和 Laves 密铺地图，共 17 种。
- 主菜单选势力和地图、随机地图预览、玩家模式、科技选择、消息面板和截图功能。
- 主菜单可配置**联盟**（盟友互不占领、互不杀伤，激光穿过盟友领土），排行榜标注各联盟的聚合统计。
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

> Linux / WSL 构建、完整命令行参数、窗口内操作、目录结构与架构概览见
> [`运行说明.md`](运行说明.md)。

## 文档

| 想了解 | 看 |
|---|---|
| 构建、运行、操作、命令行 | [`运行说明.md`](运行说明.md) |
| 地图编辑器 | [`地图编辑器说明.md`](地图编辑器说明.md) |
| 全部 config 键 | [`.docs/配置说明.md`](.docs/配置说明.md) |
| 模块与接口约定 | [`.docs/工程规范.md`](.docs/工程规范.md) |
| 写码习惯与质量门禁 | [`.docs/代码卫生.md`](.docs/代码卫生.md) |
| 文档规则 | [`.docs/文档卫生.md`](.docs/文档卫生.md) |
| 发行打包 | [`.docs/导出发行版.md`](.docs/导出发行版.md) |
| 依赖与资产许可 | [`THIRD_PARTY.md`](THIRD_PARTY.md)、[`ASSET-LICENSES.md`](ASSET-LICENSES.md) |
| 版本更新记录 | [`CHANGELOG.md`](CHANGELOG.md) |

## 当前状态

项目持续完善中。快照版本采用严格匹配策略，跨版本读档不保证兼容；窗口模式默认使用。Windows 系统
中文字体，发行包不捆绑字体。运行时需要从项目或发行包根目录启动，并保证 `userdata/` 可写。

## 参与开发

动手前先读 [`.docs/代码卫生.md`](.docs/代码卫生.md)、[`.docs/文档卫生.md`](.docs/文档卫生.md)、
[`.docs/工程规范.md`](.docs/工程规范.md)；提交流程与本地检查见 [`CONTRIBUTING.md`](CONTRIBUTING.md)。

暂时不是很会用这边的功能。
