# 更新日志

发行版使用本文件对应版本的条目作为 GitHub Release notes。发布前将 `[Unreleased]` 改为版本号和日期，并把该条目复制到 GitHub Release 描述中，再重新建立空的 `[Unreleased]` 区段。

## [Unreleased] - 2026-09-04

### 修复

- 修复多个 `FreeArmyChance` 使用不同 `param` 时后面的兵种覆盖前面的兵种；现在各兵种概率分别生效，同一兵种的概率按增益规则相加。
- 修正主菜单“添加势力”子面板可能过长的问题。

### 新功能

- 移除旧 BMP/lwmap 地图载入路径，改由面向开发者的地图编辑器创建和维护 `.landmap` 地图。

### 完善

- 完成 `ProjectileRenderer`/特效命名相关整理，统一使用更明确的特效职责划分。

### 仓库维护

- 移除非文档历史 `rubbish/` 内容、生成的密铺预览和根目录编译产物；`rubbish/docs` 恢复保留。
- 保留并恢复可复用的 `tools/view_spec.py`，其生成输出改为忽略。
- 压缩归档 2026-08 开发资料，收尾状态见 `.docs/old/2026_08_开发计划.md`。

### 发布检查

- 发布前运行配置校验、全量测试和无头烟测，并完成 `ASSET-LICENSES.md` 的资源授权审计。
