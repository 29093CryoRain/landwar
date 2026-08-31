
# landwar：领土战争

`landwar` 是一个使用AI开发, 使用了 C++20、SDL2、EnTT 和 Dear ImGui 构建的简易实时领土战争模拟游戏。
模型主要使用了deepseek-v4-flash和GPT 5.6-luna.
多个可配置 AI 势力在陆地、海洋、城市和山地组成的地图上持续扩张、交战和争夺首都。

## 项目特点

- 固定步长模拟与确定性随机数：指定相同的地图种子、主种子、配置和选项即可复现同一局面。
- 配置驱动的势力、兵种、科技、经济、地图和渲染参数。
- 正方形、六边形、三角形，以及半正和 Laves 密铺地图(排除两种过于简单无聊的)，共17种。
- 普通兵、先锋兵、开拓兵、激光兵、爆炸兵、地雷兵、手枪兵和霰弹兵。
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

Linux 构建和测试由 GitHub Actions 自动执行。完整依赖、版本和许可证说明见
[`THIRD_PARTY.md`](../THIRD_PARTY.md)，构建与运行细节见 [`README.md`](../README.md)。

## 配置与扩展

`data/` 中的 JSONC 文件可以调整游戏规则。可以在 `data/factions.jsonc` 中追加连续 ID 的势力：

```jsonc
{
  "id": 9,
  "name": "新势力",
  "description": "用于测试的势力。",
  "nameColors": ["primary"],
  "color": [240, 80, 80],
  "secondary": [255, 220, 220],
  "unitPreference": {"vanguard": 2.0},
  "buffs": []
}
```

ID 0 保留给中立势力，可玩势力最多 63 个，ID 必须从 0 开始连续。修改后运行：

```bash
build-release/landwar.exe --validate-config
```

## 当前状态

这是一个持续完善中的项目。快照版本采用严格匹配策略，跨版本读档不保证兼容；窗口模式默认使用
Windows 系统中文字体，发行包不捆绑字体。运行时需要从项目或发行包根目录启动，并保证 `userdata/`
可写。

## 参与开发

Bug 和功能建议请使用 GitHub Issue 模板，并附上提交版本、运行模式、命令行、配置/地图信息和复现步骤。
代码贡献前请阅读 [`CONTRIBUTING.md`](../CONTRIBUTING.md)。项目源代码遵循 MIT License，其他组件和资源
的说明见 [`LICENSE`](../LICENSE)、[`THIRD_PARTY.md`](../THIRD_PARTY.md) 及 [`ASSET-LICENSES.md`](../ASSET-LICENSES.md)。
