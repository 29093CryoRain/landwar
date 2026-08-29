// Paths.h — 路径约定（2026-08 工程改进：资产与运行期产物分离）。
//
// 资产（只读、随源码分发，位于 data/）：
//   map_*.bmp、army*.png、config.json 及其配置分片、towers.png 等。
// 运行期产物（可写、可丢弃、不进版本库，位于 userdata/）：
//   userdata/options.json        菜单选项（开始游戏时保存、启动读取）
//   userdata/maps/gen_*.bmp      随机地图生成物（P6，确定性：seed+参数 → 同名文件）
//   userdata/screenshots/        F12 / QA 钩子截图
//
// 约定：从项目根运行（data/ 相对 CWD 解析）是既定约束（见 README）。
// 本模块只提供路径字符串与目录创建；文件读写仍由各调用方负责。
#pragma once

#include <string>

namespace lw {

// ---- 目录 ----
constexpr const char* kAssetDataDir = "data";              // 只读资产目录
constexpr const char* kUserDataDir = "userdata";           // 运行期产物根目录
constexpr const char* kGeneratedMapDir = "userdata/maps";  // 随机地图生成物
constexpr const char* kScreenshotDir = "userdata/screenshots";  // 截图

// ---- 默认文件路径（散落各处的字面量收敛到此，避免改一处漏一处）----
inline const std::string kDefaultConfigPath = "data/config.json";
// Config 长段（由 Config::loadFromFile 按核心配置所在目录加载）。
inline const std::string kRenderConfigPath = "data/render.json";
inline const std::string kTechConfigPath = "data/techs.json";
inline const std::string kFactionsConfigPath = "data/factions.json";
inline const std::string kUnitsConfigPath = "data/units.json";
// 全部密铺城市等级/形状表（2026-08-26 起外置；P1.2 含 square/hex/tri；tools/gen_city_shapes_json.py 生成）。
inline const std::string kCityShapesPath = "data/city_shapes.json";
// 城市贴图预计算缩放/竖直平移表（2026-08-26 自 config.json render.city 外置；由
// tools/city_icon_fit_tool + tools/apply_city_icon_fit_scales.py 生成）。
inline const std::string kCityIconFitsPath = "data/city_icon_fits.json";
inline const std::string kDefaultOptionsPath = "userdata/options.json";
inline const std::string kDefaultMapFile = "data/map_bigIslands.bmp";  // 默认地图（资产）

// 确保目录存在（按 '/' 或 '\\' 逐级创建；已存在视为成功）。失败返回 false。
bool ensureDirExists(const std::string& path);

// 确保 userdata/ 三个子目录存在（应用启动时调用一次；失败仅记日志，不阻断）。
bool ensureUserDataDirs();

}  // namespace lw
