# GitHub 发布前改进建议

## 必做

- [x] 增加许可证文件，明确代码、图片、地图和第三方依赖的授权边界（`LICENSE`、`ASSET-LICENSES.md`、`THIRD_PARTY.md`）。
- [x] 增加 GitHub Actions：Linux 构建、单元测试、警告即错误和 `--headless` 烟测。
- [x] 记录 ImGui、EnTT 版本并在 CI 使用固定 tag；SDL2 等系统包仍随工具链管理，发行二进制需记录实际包版本。
- [x] 将发布目录制作改成可复用脚本，并提供配置、无头、可写目录和可选窗口截图验证：`tools/package_release.ps1`。
- [x] 在 README 增加贡献方式、问题反馈模板、已知限制和配置扩展示例，说明势力 ID 必须连续、最多 63 个可玩势力。
- [ ] 完成所有图片/地图/字体的版权来源审计；地图已确认是手绘且可随项目再分发，其他图片仍见 `ASSET-LICENSES.md`。

## 建议完善

- [x] 增加配置 schema 和独立 `--validate-config` 命令，将当前依赖日志的错误反馈变成可自动化检查。
- 为动态势力增加专门测试：追加势力、菜单/选项往返、首都数量、爆炸半径特性、免费产兵、渲染调色板和快照往返。
- [x] 为快照明确版本策略：当前 v10 快照严格拒绝其他版本，不做隐式迁移；格式变化时必须显式升级版本与测试。
- [x] 在 `.docs/工程规范.md`、`.docs/配置说明.md` 和运行时校验中记录固定容量 `kMaxFactionCount=64`、
  统计数组/基础格位掩码上限及失败原因。
- [x] 在 CI 中执行编译警告检查（`-Wall -Wextra -Werror`）；格式检查仍待统一 `.clang-format` 规则后加入。
- 添加最小可运行资源包或测试资源，减少新贡献者必须依赖本机 MSYS2 路径和完整美术资源的问题。

## 发布前检查清单

- [x] Windows Release 构建及独立临时发行目录验证成功；Linux 构建由 CI runner 执行。
- [x] 全量测试通过（441 项）；配置/行为变化已同步黄金数据和语义基线记录。
- [x] `--headless --seed 42 --ticks 1000 --summary` 退出码为 0。
- [x] 窗口启动、资源加载、截图流程可用；菜单人工操作仍需发布者按清单复核。
- [x] `build-*`、`userdata/`、临时地图和本机配置已忽略，临时发行包可从根目录启动。
- [x] 发行脚本只复制声明的程序、依赖和资源；发布者仍需在提交前检查包内容。

## 已补全的发布入口

- 配置严格校验：`landwar --validate-config [--config PATH]`。
- 发布包：`powershell -File tools/package_release.ps1 -Msys2Bin <MSYS2\\ucrt64\\bin> -Verify`。
- 窗口截图验证：在上述命令后追加 `-VerifyWindow`（需要可用桌面会话）。
- CI：`.github/workflows/ci.yml`。
