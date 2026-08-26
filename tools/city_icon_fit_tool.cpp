// city_icon_fit_tool.cpp — 交互式城市贴图缩放调整工具（半正/Laves 渲染调整阶段）。
//
// 运行后生成窗口：左侧 ImGui 控制面板，右侧显示区按游戏方式画出当前密铺的一小块地图、
// 当前城市基建地块外围细线，以及按滑块缩放的等级塔贴图。
//
// 交互：
//   - 滑块：调整当前城市贴图的缩放倍率（世界单位，与 render.city.iconFitScale 同语义）。
//   - 上一个/下一个：在当前密铺地图内依次切换城市条目（同一等级多个形状变体、
//     同一形状不同锚朝向/正反三角都会各自成为一条）。
//   - 上一批/下一批：切换密铺地图（hex/tri/10 种半正+Laves）。
//   - 保存：把当前条目缩放倍率临时保存到 data/city_icon_fit_scales.json（不是 config.json）。
//
// 全部条目人工调整并保存后，运行：
//     python3 tools/apply_city_icon_fit_scales.py
// 该程序会对同一贴图等级下的所有已保存条目取最小值，并写回 data/config.json 的
// render.city.iconFitScale。
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <imgui.h>

#include "core/Config.h"
#include "core/GameDefs.h"
#include "ui/ImGuiSetup.h"
#include "world/tiling/CityIconFitter.h"
#include "world/tiling/Tiling.h"

namespace {

using nlohmann::ordered_json;
using nlohmann::json;

constexpr int kLogicalW = 1500;
constexpr int kLogicalH = 900;
constexpr int kPanelW = 380;
constexpr int kViewX = kPanelW + 20;
constexpr int kViewY = 20;
constexpr int kViewW = kLogicalW - kPanelW - 40;
constexpr int kViewH = kLogicalH - 40;
constexpr int kMapCols = 8;
constexpr int kMapRows = 8;
constexpr int kAnchorR = 4;
constexpr int kAnchorC = 4;
constexpr const char* kSavePath = "data/city_icon_fit_scales.json";
constexpr int kSlotCount = 4;  // 暂存区数量

const std::vector<lw::TilingType> kToolTilings = {
    lw::TilingType::Hex,        lw::TilingType::Tri,        lw::TilingType::Arch33336,
    lw::TilingType::Arch33434,  lw::TilingType::Arch3464,   lw::TilingType::Arch3636,
    lw::TilingType::Arch31212,  lw::TilingType::Arch4612,   lw::TilingType::Arch488,
    lw::TilingType::Laves3636,  lw::TilingType::Laves31212, lw::TilingType::Laves4612,
    lw::TilingType::Laves488,   lw::TilingType::Laves33434, lw::TilingType::Laves33336,
    lw::TilingType::Laves3464};

struct Item {
    lw::TilingType tiling = lw::TilingType::Hex;
    int shapeIndex = 0;    // set.shapes 下标
    int levelIndex = 0;    // set.levels 下标
    int variantIndex = 0;  // 同级第几个变体（0,1,...）
    double level = 0.0;
    int iconLevel = 1;
    int anchorB = 0;  // hex=0；tri=0正/1倒；表驱动=基础格 b
    std::string anchorDesc;
};

// 锚基础格类别：一个 baseGroups 数组算一类（如 "n3ua":[2,11] = 一类）。每类一个条目，
// anchor 取该类最小基础格。从 data/city_shapes.json 读回（Config 只留掩码、丢失组名）。
struct AnchorClass {
    std::string name;  // 组名；纯数字锚无组时 = "base_<b>"
    int anchorB;
};

// 每密铺每个形状（跳过 disabled，与 Config::parseTilingSetJson 次序一致）→ 其锚基础格类别列表。
std::unordered_map<std::string, std::vector<std::vector<AnchorClass>>> g_anchorClasses;

void loadAnchorClasses() {
    std::ifstream ifs("data/city_shapes.json");
    if (!ifs.is_open()) return;
    json root;
    try {
        ifs >> root;
    } catch (const std::exception&) {
        return;
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().is_object()) continue;
        const auto& tv = it.value();
        std::unordered_map<std::string, std::vector<int>> bg;
        if (tv.contains("baseGroups") && tv["baseGroups"].is_object())
            for (auto g = tv["baseGroups"].begin(); g != tv["baseGroups"].end(); ++g) {
                if (!g.key().empty() && g.key()[0] == '_') continue;  // 下划线键为注释
                std::vector<int> bases;
                if (g.value().is_array())
                    for (const auto& b : g.value())
                        if (b.is_number()) bases.push_back(b.get<int>());
                bg[g.key()] = std::move(bases);
            }
        std::unordered_map<std::string, int> groupMin;
        std::unordered_map<int, std::string> baseGroup;
        for (auto& [nm, bases] : bg) {
            if (bases.empty()) continue;
            groupMin[nm] = *std::min_element(bases.begin(), bases.end());
            for (int b : bases) baseGroup[b] = nm;
        }
        std::vector<std::vector<AnchorClass>> shapeClasses;
        if (tv.contains("shapes") && tv["shapes"].is_array())
            for (const auto& shp : tv["shapes"]) {
                if (shp.contains("disabled") && shp["disabled"].is_boolean() &&
                    shp["disabled"].get<bool>())
                    continue;  // 与 parseTilingSetJson 一致：disabled 形状不生成条目
                std::vector<AnchorClass> cls;
                std::unordered_set<std::string> seen;
                if (shp.contains("anchorBases") && shp["anchorBases"].is_array()) {
                    for (const auto& e : shp["anchorBases"]) {
                        if (e.is_string()) {
                            const std::string nm = e.get<std::string>();
                            if (seen.count(nm) || !bg.count(nm)) continue;
                            seen.insert(nm);
                            cls.push_back({nm, groupMin[nm]});
                        } else if (e.is_number()) {
                            const int b = e.get<int>();
                            const std::string nm =
                                baseGroup.count(b) ? baseGroup[b] : ("base_" + std::to_string(b));
                            if (seen.count(nm)) continue;
                            seen.insert(nm);
                            cls.push_back({nm, baseGroup.count(b) ? groupMin[baseGroup[b]] : b});
                        }
                    }
                }
                if (cls.empty()) cls.push_back({"any", 0});
                shapeClasses.push_back(std::move(cls));
            }
        g_anchorClasses[it.key()] = std::move(shapeClasses);
    }
}

struct WorldEdge {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct CurrentView {
    std::vector<int> cells;
    std::vector<lw::FitPoly> polys;
    std::vector<WorldEdge> boundaryEdges;
    double centerX = 0.0;
    double centerY = 0.0;
    double blockSize = 40.0;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    double autoScale = 1.0;
    double autoOffsetY = 0.0;  // 自动建议的竖直平移（与 autoScale 配套）
    int iconLevel = 1;
};

std::string readFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::string itemKey(const Item& it) {
    return std::string(lw::tilingName(it.tiling)) + "|" + std::to_string(it.levelIndex) + "|"
           + std::to_string(it.shapeIndex) + "|" + std::to_string(it.anchorB);
}

// 按 Map::shapeCells 的非方形规则解析出实际格下标。失败返回 false。
bool resolveCityCells(const lw::TilingGeom& geom, const lw::Config::City::Shape& sh,
                      const Item& it, std::vector<int>& cells) {
    const int anchorIndex = geom.cellIndexAt(kAnchorR, kAnchorC, it.anchorB);
    if (anchorIndex < 0) return false;
    double ax = 0.0, ay = 0.0;
    geom.cellCenter(anchorIndex, ax, ay);
    cells.clear();
    cells.reserve(sh.cells.size());
    for (const auto& sc : sh.cells) {
        double wx = 0.0, wy = 0.0;
        if (geom.type == lw::TilingType::Hex) {
            wx = ax + lw::TilingGeom::kHexColSpacing * (sc.dx + 0.5 * sc.dy);
            wy = ay + lw::TilingGeom::kHexRowSpacing * sc.dy;
        } else if (geom.type == lw::TilingType::Tri) {
            const bool anchorUp = (anchorIndex & 1) == 0;
            wx = ax + sc.dx * lw::TilingGeom::kTriSide;
            wy = ay + (anchorUp ? sc.dy : -sc.dy) * lw::TilingGeom::kTriAlt;
        } else {
            // cells 已是世界坐标（2026-08-26 方案A 烘入），锚格朝向由数据保证，不再旋转。
            wx = ax + sc.dx;
            wy = ay + sc.dy;
        }
        const int idx = geom.worldToCell(wx, wy);
        if (idx < 0) return false;
        cells.push_back(idx);
    }
    return true;
}

bool buildCurrentView(const Item& it, const lw::TilingGeom& geom,
                      const lw::Config::City::Shape& sh, CurrentView& v) {
    v.cells.clear();
    v.polys.clear();
    v.boundaryEdges.clear();
    if (!resolveCityCells(geom, sh, it, v.cells)) return false;

    std::unordered_set<int> inShape(v.cells.begin(), v.cells.end());
    double sx = 0.0, sy = 0.0;
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    double vx[16], vy[16];
    int n = 0;
    for (int idx : v.cells) {
        double ccx = 0.0, ccy = 0.0;
        geom.cellCenter(idx, ccx, ccy);
        sx += ccx;
        sy += ccy;
        ++n;
        const int nv = geom.cellPolygon(idx, vx, vy, 16);
        if (nv < 3) return false;
        lw::FitPoly poly;
        poly.reserve(static_cast<std::size_t>(nv));
        for (int k = 0; k < nv; ++k) {
            poly.push_back({vx[k], vy[k]});
            minX = std::min(minX, vx[k]);
            maxX = std::max(maxX, vx[k]);
            minY = std::min(minY, vy[k]);
            maxY = std::max(maxY, vy[k]);
        }
        v.polys.push_back(std::move(poly));
    }
    if (n == 0) return false;
    v.centerX = sx / static_cast<double>(n);
    v.centerY = sy / static_cast<double>(n);
    v.minX = minX;
    v.minY = minY;
    v.maxX = maxX;
    v.maxY = maxY;

    // 城市基建地块外边界（邻格不在形状内的格边）。
    for (int idx : v.cells) {
        const int nk = geom.neighborCount(idx);
        for (int k = 0; k < nk; ++k) {
            const int nb = geom.neighbor(idx, k);
            if (nb >= 0 && inShape.count(nb) != 0) continue;
            double ex0 = 0, ey0 = 0, ex1 = 0, ey1 = 0;
            if (geom.cellEdge(idx, k, ex0, ey0, ex1, ey1))
                v.boundaryEdges.push_back({ex0, ey0, ex1, ey1});
        }
    }
    return true;
}

void loadSaved(std::unordered_map<std::string, double>& savedScale,
                std::unordered_map<std::string, double>& savedOffsetY,
                float& lastScale, float& lastOffsetY, bool& hasLastSaved,
                std::array<float, kSlotCount>& slotScale,
                std::array<float, kSlotCount>& slotOffsetY,
                std::array<bool, kSlotCount>& slotUsed) {
    const std::string text = readFile(kSavePath);
    if (text.empty()) return;
    try {
        const ordered_json j = ordered_json::parse(text);
        if (j.contains("records") && j["records"].is_array()) {
            for (const auto& rec : j["records"]) {
                if (!rec.contains("key") || !rec["key"].is_string()) continue;
                const std::string key = rec["key"].get<std::string>();
                if (rec.contains("scale") && rec["scale"].is_number())
                    savedScale[key] = rec["scale"].get<double>();
                if (rec.contains("offsetY") && rec["offsetY"].is_number())
                    savedOffsetY[key] = rec["offsetY"].get<double>();
            }
        }
        if (j.contains("lastSaved") && j["lastSaved"].is_object()) {
            if (j["lastSaved"].contains("scale") && j["lastSaved"]["scale"].is_number()
                && j["lastSaved"].contains("offsetY") && j["lastSaved"]["offsetY"].is_number()) {
                lastScale = j["lastSaved"]["scale"].get<float>();
                lastOffsetY = j["lastSaved"]["offsetY"].get<float>();
                hasLastSaved = true;
            }
        }
        if (j.contains("slots") && j["slots"].is_array()) {
            const std::size_t n = std::min<std::size_t>(j["slots"].size(), kSlotCount);
            for (std::size_t i = 0; i < n; ++i) {
                const auto& rec = j["slots"][i];
                if (!rec.is_object()) continue;
                if (rec.contains("used") && rec["used"].is_boolean())
                    slotUsed[i] = rec["used"].get<bool>();
                if (rec.contains("scale") && rec["scale"].is_number())
                    slotScale[i] = rec["scale"].get<float>();
                if (rec.contains("offsetY") && rec["offsetY"].is_number())
                    slotOffsetY[i] = rec["offsetY"].get<float>();
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("city_icon_fit_tool: 读取临时保存文件失败: {}", e.what());
    }
}

void saveJsonFile(const std::vector<std::vector<Item>>& itemsByBatch,
                  const std::unordered_map<std::string, double>& savedScale,
                  const std::unordered_map<std::string, double>& savedOffsetY,
                  float lastScale, float lastOffsetY, bool hasLastSaved,
                  const std::array<float, kSlotCount>& slotScale,
                  const std::array<float, kSlotCount>& slotOffsetY,
                  const std::array<bool, kSlotCount>& slotUsed) {
    ordered_json j;
    j["_comment"] =
        "交互工具临时数据：每一条 = 某密铺下某个城市等级的一个具体形状/锚朝向变体。"
        "scale 按 密铺+贴图等级 取最小值写回 config；offsetY 逐条目独立写回 config。";
    if (hasLastSaved) {
        j["lastSaved"] = {{"scale", lastScale}, {"offsetY", lastOffsetY}};
    }
    j["slots"] = ordered_json::array();
    for (int i = 0; i < kSlotCount; ++i) {
        j["slots"].push_back({{"used", slotUsed[static_cast<std::size_t>(i)]},
                              {"scale", slotScale[static_cast<std::size_t>(i)]},
                              {"offsetY", slotOffsetY[static_cast<std::size_t>(i)]}});
    }
    j["records"] = ordered_json::array();
    for (const auto& items : itemsByBatch) {
        for (const auto& it : items) {
            const std::string key = itemKey(it);
            const auto sit = savedScale.find(key);
            if (sit == savedScale.end()) continue;
            ordered_json rec;
            rec["key"] = key;
            rec["tiling"] = lw::tilingName(it.tiling);
            rec["levelIndex"] = it.levelIndex;
            rec["level"] = it.level;
            rec["iconLevel"] = it.iconLevel;
            rec["shapeIndex"] = it.shapeIndex;
            rec["variantIndex"] = it.variantIndex;
            rec["anchorB"] = it.anchorB;
            rec["anchorDesc"] = it.anchorDesc;
            rec["scale"] = sit->second;
            const auto oit = savedOffsetY.find(key);
            rec["offsetY"] = (oit != savedOffsetY.end()) ? oit->second : 0.0;
            j["records"].push_back(std::move(rec));
        }
    }
    std::ofstream ofs(kSavePath, std::ios::binary | std::ios::trunc);
    ofs << j.dump(2, ' ', false) << "\n";
    spdlog::info("city_icon_fit_tool: 已保存 {} 条记录 -> {}", j["records"].size(), kSavePath);
}

// 把世界坐标映射到显示区（城市几何中心固定画在显示区中央）。
int toScreenX(double wx, const CurrentView& v) {
    return static_cast<int>(std::lround(kViewX + kViewW * 0.5 + (wx - v.centerX) * v.blockSize));
}
int toScreenY(double wy, const CurrentView& v) {
    return static_cast<int>(std::lround(kViewY + kViewH * 0.5 - (wy - v.centerY) * v.blockSize));
}

void fillPolygon(SDL_Renderer* ren, const std::vector<SDL_Point>& pts, SDL_Color col) {
    if (pts.size() < 3) return;
    std::vector<SDL_Vertex> verts;
    verts.reserve(pts.size());
    for (const auto& p : pts) {
        SDL_Vertex v{};
        v.position = {static_cast<float>(p.x), static_cast<float>(p.y)};
        v.color = col;
        v.tex_coord = {0.0f, 0.0f};
        verts.push_back(v);
    }
    std::vector<int> idx;
    idx.reserve((pts.size() - 2) * 3);
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        idx.push_back(0);
        idx.push_back(static_cast<int>(i));
        idx.push_back(static_cast<int>(i + 1));
    }
    SDL_RenderGeometry(ren, nullptr, verts.data(), static_cast<int>(verts.size()), idx.data(),
                       static_cast<int>(idx.size()));
}

void drawScene(SDL_Renderer* ren, const lw::TilingGeom& geom, const CurrentView& v,
               const std::vector<int>& cityCells, SDL_Texture* cityTex, double aspect,
               float sliderScale, float offsetY) {
    const SDL_Rect view{kViewX, kViewY, kViewW, kViewH};
    SDL_RenderSetClipRect(ren, &view);

    std::unordered_set<int> inShape(cityCells.begin(), cityCells.end());
    const SDL_Color landA{68, 122, 68, 255};
    const SDL_Color landB{78, 134, 78, 255};
    const SDL_Color cityCol{112, 168, 112, 255};

    const int cellCount = geom.cellCount();
    for (int idx = 0; idx < cellCount; ++idx) {
        double wx[16], wy[16];
        const int nv = geom.cellPolygon(idx, wx, wy, 16);
        if (nv < 3) continue;
        std::vector<SDL_Point> pts;
        pts.reserve(static_cast<std::size_t>(nv));
        bool visible = false;
        for (int k = 0; k < nv; ++k) {
            const int sx = toScreenX(wx[k], v);
            const int sy = toScreenY(wy[k], v);
            pts.push_back({sx, sy});
            if (sx >= kViewX && sx <= kViewX + kViewW && sy >= kViewY && sy <= kViewY + kViewH)
                visible = true;
        }
        if (!visible) continue;
        fillPolygon(ren, pts, inShape.count(idx) != 0 ? cityCol : (idx & 1 ? landA : landB));
    }

    // 城市贴图：中心 = 基建地块几何中心，宽 = 滑块缩放 × blockSize。
    if (cityTex) {
        const int dstW = std::max(1, static_cast<int>(std::lround(sliderScale * v.blockSize)));
        const int dstH = std::max(1, static_cast<int>(std::lround(dstW * aspect)));
        const int cx = kViewX + kViewW / 2;
        const int cy = kViewY + kViewH / 2 - static_cast<int>(std::lround(offsetY * v.blockSize));
        const SDL_Rect dst{cx - dstW / 2, cy - dstH / 2, dstW, dstH};
        SDL_SetTextureBlendMode(cityTex, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(ren, cityTex, nullptr, &dst);
    } else {
        const int dstW = std::max(1, static_cast<int>(std::lround(sliderScale * v.blockSize)));
        const int dstH = std::max(1, static_cast<int>(std::lround(dstW * aspect)));
        const int cx = kViewX + kViewW / 2;
        const int cy = kViewY + kViewH / 2 - static_cast<int>(std::lround(offsetY * v.blockSize));
        const SDL_Rect dst{cx - dstW / 2, cy - dstH / 2, dstW, dstH};
        SDL_SetRenderDrawColor(ren, 220, 220, 220, 255);
        SDL_RenderDrawRect(ren, &dst);
    }

    // 城市基建地块外围细线（亮色，2px）。
    SDL_SetRenderDrawColor(ren, 255, 235, 140, 255);
    for (const auto& e : v.boundaryEdges) {
        const int x0 = toScreenX(e.x0, v);
        const int y0 = toScreenY(e.y0, v);
        const int x1 = toScreenX(e.x1, v);
        const int y1 = toScreenY(e.y1, v);
        SDL_RenderDrawLine(ren, x0, y0, x1, y1);
        SDL_RenderDrawLine(ren, x0, y0 + 1, x1, y1 + 1);
    }

    SDL_RenderSetClipRect(ren, nullptr);
}

}  // namespace

int main(int, char**) {
    const std::string cfgText = readFile("data/config.json");
    lw::Config cfg = lw::Config::loadFromJson(cfgText);
    lw::Config::loadCityIconFits(cfg);  // iconFitScale/offsetY 已外置到 data/city_icon_fits.json
    loadAnchorClasses();  // 生条目需 baseGroups 组名（Config 只留掩码）

    // 构建批次（密铺地图）与条目（城市等级/形状变体/锚朝向）。
    std::vector<lw::TilingGeom> geoms;
    std::vector<std::vector<Item>> itemsByBatch;
    for (const lw::TilingType tt : kToolTilings) {
        lw::TilingGeom geom;
        geom.type = tt;
        geom.cols = kMapCols;
        geom.rows = kMapRows;
        if (tt > lw::TilingType::Tri && geom.baseCount() <= 0) continue;

        const auto& set = cfg.city.setFor(tt);
        if (set.shapes.empty() || set.levels.empty()) continue;

        std::vector<Item> items;
        for (std::size_t si = 0; si < set.shapes.size(); ++si) {
            int li = (si < set.shapeLevelIndex.size())
                         ? set.shapeLevelIndex[si]
                         : static_cast<int>(std::min<std::size_t>(si, set.levels.size() - 1));
            if (li < 0 || li >= static_cast<int>(set.levels.size())) continue;
            const double level = set.levels[static_cast<std::size_t>(li)];
            const int iconLevel = set.iconLevelFor(level);

            int variantIndex = 0;
            for (std::size_t prev = 0; prev < si; ++prev) {
                int pl = (prev < set.shapeLevelIndex.size())
                             ? set.shapeLevelIndex[prev]
                             : static_cast<int>(std::min<std::size_t>(prev, set.levels.size() - 1));
                if (pl == li) ++variantIndex;
            }

            auto push = [&](int anchorB, const std::string& desc) {
                Item it;
                it.tiling = tt;
                it.shapeIndex = static_cast<int>(si);
                it.levelIndex = li;
                it.variantIndex = variantIndex;
                it.level = level;
                it.iconLevel = iconLevel;
                it.anchorB = anchorB;
                it.anchorDesc = desc;
                items.push_back(it);
            };

            if (tt == lw::TilingType::Hex) {
                push(0, "六边形锚");
            } else if (tt == lw::TilingType::Tri) {
                push(0, "正三角锚");
                push(1, "倒三角锚");
            } else {
                // 半正/Laves：每城市每**类锚基础格**一条（baseGroups 每个数组 = 一类，如
                // "n3ua":[2,11] 一类）。锚取该类最小基础格；形状已烘焙世界帧，同类别锚几何一致。
                const auto itCls = g_anchorClasses.find(lw::tilingName(tt));
                if (itCls != g_anchorClasses.end() && itCls->second.size() > si) {
                    for (const AnchorClass& c : itCls->second[si]) {
                        const int idx = geom.cellIndexAt(kAnchorR, kAnchorC, c.anchorB);
                        push(c.anchorB, c.name + " (锚 " + std::to_string(c.anchorB) + ", " +
                                             std::to_string(idx >= 0 ? geom.neighborCount(idx) : 0) +
                                             "边)");
                    }
                } else {
                    push(0, "参考基础格 0");
                }
            }
        }
        if (!items.empty()) {
            geoms.push_back(geom);
            itemsByBatch.push_back(std::move(items));
        }
    }
    if (geoms.empty()) {
        spdlog::error("city_icon_fit_tool: 没有可用的密铺地图");
        return 1;
    }

    // SDL / ImGui 初始化。
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        spdlog::critical("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        spdlog::critical("IMG_Init PNG failed: {}", IMG_GetError());
        SDL_Quit();
        return 1;
    }

    int dispW = 1280, dispH = 800;
    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(0, &bounds) == 0) {
        dispW = bounds.w;
        dispH = bounds.h;
    }
    const double winScale =
        std::min(1.0, std::min(0.92 * dispW / static_cast<double>(kLogicalW),
                               0.92 * dispH / static_cast<double>(kLogicalH)));
    SDL_Window* win = SDL_CreateWindow(
        "city_icon_fit_tool", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(kLogicalW * winScale), static_cast<int>(kLogicalH * winScale),
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        spdlog::critical("SDL_CreateWindow failed: {}", SDL_GetError());
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        spdlog::critical("SDL_CreateRenderer failed: {}", SDL_GetError());
        SDL_DestroyWindow(win);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(ren, kLogicalW, kLogicalH);

    if (!lw::ui::initImGui(win, ren)) {
        spdlog::critical("ImGui init failed");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    // 加载 1..10 级塔贴图。
    std::array<SDL_Texture*, 11> texs{};
    std::array<double, 11> aspects{};
    for (int lv = 1; lv <= 10; ++lv) {
        const std::string path = "data/tower/tower" + std::to_string(lv) + ".png";
        texs[static_cast<std::size_t>(lv)] = IMG_LoadTexture(ren, path.c_str());
        if (texs[static_cast<std::size_t>(lv)]) {
            int tw = 0, th = 0;
            SDL_QueryTexture(texs[static_cast<std::size_t>(lv)], nullptr, nullptr, &tw, &th);
            if (tw > 0 && th > 0)
                aspects[static_cast<std::size_t>(lv)] = static_cast<double>(th) / static_cast<double>(tw);
            SDL_SetTextureBlendMode(texs[static_cast<std::size_t>(lv)], SDL_BLENDMODE_BLEND);
        } else {
            spdlog::warn("city_icon_fit_tool: 加载 {} 失败: {}", path, IMG_GetError());
        }
    }

    std::unordered_map<std::string, double> savedScale;
    std::unordered_map<std::string, double> savedOffsetY;
    float lastSavedScale = 1.0f;
    float lastSavedOffsetY = 0.0f;
    bool hasLastSaved = false;
    std::array<float, kSlotCount> slotScale{};
    std::array<float, kSlotCount> slotOffsetY{};
    std::array<bool, kSlotCount> slotUsed{};
    loadSaved(savedScale, savedOffsetY, lastSavedScale, lastSavedOffsetY, hasLastSaved,
              slotScale, slotOffsetY, slotUsed);

    int batchIndex = 0;
    int itemIndex = 0;
    CurrentView view;
    float sliderScale = 1.0f;
    float sliderMax = 4.0f;
    float sliderOffsetY = 0.0f;
    bool currentSaved = false;
    bool dirty = false;

    auto loadItem = [&]() {
        const Item& it = itemsByBatch[static_cast<std::size_t>(batchIndex)]
                            [static_cast<std::size_t>(itemIndex)];
        const auto& set = cfg.city.setFor(it.tiling);
        const auto* sh = set.shapeFor(it.level, it.variantIndex);
        if (!sh) return;
        if (!buildCurrentView(it, geoms[static_cast<std::size_t>(batchIndex)], *sh, view)) return;

        const double aspect = aspects[static_cast<std::size_t>(it.iconLevel)] > 0.0
                                  ? aspects[static_cast<std::size_t>(it.iconLevel)]
                                  : 1.0;
        const double aabbW = view.maxX - view.minX;
        const double aabbH = view.maxY - view.minY;
        const double pad = std::max(0.8, 0.3 * std::max(aabbW, aabbH));
        view.blockSize = std::clamp(
            std::min(kViewW / (aabbW + 2.0 * pad), kViewH / (aabbH + 2.0 * pad)), 8.0, 120.0);
        view.autoScale = lw::CityIconFitter::maxScaleAt(view.polys, 1.0, aspect, view.centerX,
                                                        view.centerY);
        view.autoOffsetY = 0.0;
        if (view.autoScale <= 0.0) {
            view.autoScale = std::max(0.01, std::min(aabbW, aabbH / aspect));
        } else {
            // 某些形状（如 arch_3636 L5）在不同锚朝向下视觉相同，但格心平均中心
            // 恰落在内部边/顶点上，固定该中心会算出过小的内接矩形。
            // 只补试一个竖直中心（AABB 中心）：若更好则采用，并同步给出竖直偏移。
            const double aabbFit = std::max(0.01, std::min(aabbW, aabbH / aspect));
            if (view.autoScale < 0.75 * aabbFit) {
                const double aabbCy = 0.5 * (view.minY + view.maxY);
                const double sMid = lw::CityIconFitter::maxScaleAt(
                    view.polys, 1.0, aspect, view.centerX, aabbCy);
                if (sMid > view.autoScale) {
                    view.autoScale = sMid;
                    view.autoOffsetY = aabbCy - view.centerY;
                }
            }
        }
        view.iconLevel = it.iconLevel;

        const std::string key = itemKey(it);
        const auto sit = savedScale.find(key);
        // 初始缩放：优先 config 的 render.city.iconFitScale[tiling][iconLevel]（= 游戏实际使用的
        // 世界单位缩放，使显示与游戏一致）；其次手动保存值；最后用 CityIconFitter 自动建议。
        double initScale = view.autoScale;
        const auto tileIt = cfg.render.city.iconFitScale.find(lw::tilingName(it.tiling));
        if (tileIt != cfg.render.city.iconFitScale.end()) {
            const auto lvIt = tileIt->second.find(it.iconLevel);
            if (lvIt != tileIt->second.end() && lvIt->second > 0.0) initScale = lvIt->second;
        }
        if (sit != savedScale.end() && savedScale.count(key) != 0) initScale = sit->second;
        const auto oit = savedOffsetY.find(key);
        const double initOff = (oit != savedOffsetY.end()) ? oit->second : view.autoOffsetY;
        sliderScale = static_cast<float>(initScale);
        sliderOffsetY = static_cast<float>(initOff);
        sliderMax = static_cast<float>(
            std::max(2.0, std::max(view.autoScale * 2.5, initScale * 2.0)));
        currentSaved = (savedScale.count(key) != 0);
        dirty = false;
    };

    auto saveCurrentIfDirty = [&]() {
        if (!dirty) return;
        const Item& cur = itemsByBatch[static_cast<std::size_t>(batchIndex)]
                              [static_cast<std::size_t>(itemIndex)];
        const std::string key = itemKey(cur);
        savedScale[key] = sliderScale;
        savedOffsetY[key] = sliderOffsetY;
        lastSavedScale = sliderScale;
        lastSavedOffsetY = sliderOffsetY;
        hasLastSaved = true;
        saveJsonFile(itemsByBatch, savedScale, savedOffsetY, lastSavedScale, lastSavedOffsetY,
                     hasLastSaved, slotScale, slotOffsetY, slotUsed);
        currentSaved = true;
        dirty = false;
    };
    auto changeItem = [&](int dir) {
        const auto& items = itemsByBatch[static_cast<std::size_t>(batchIndex)];
        if (items.empty()) return;
        saveCurrentIfDirty();
        const int n = static_cast<int>(items.size());
        itemIndex = (itemIndex + dir + n) % n;
        loadItem();
    };
    auto changeBatch = [&](int dir) {
        const int n = static_cast<int>(itemsByBatch.size());
        if (n <= 0) return;
        saveCurrentIfDirty();
        batchIndex = (batchIndex + dir + n) % n;
        itemIndex = 0;
        loadItem();
    };

    loadItem();

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            lw::ui::processImGuiEvent(ev);
            if (ev.type == SDL_QUIT) {
                saveCurrentIfDirty();
                quit = true;
            }
        }

        SDL_SetRenderDrawColor(ren, 32, 34, 40, 255);
        SDL_RenderClear(ren);

        const Item& it = itemsByBatch[static_cast<std::size_t>(batchIndex)]
                            [static_cast<std::size_t>(itemIndex)];
        drawScene(ren, geoms[static_cast<std::size_t>(batchIndex)], view, view.cells,
                  texs[static_cast<std::size_t>(it.iconLevel)],
                  aspects[static_cast<std::size_t>(it.iconLevel)] > 0.0
                      ? aspects[static_cast<std::size_t>(it.iconLevel)]
                      : 1.0,
                  sliderScale, sliderOffsetY);

        lw::ui::beginImGuiFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(kPanelW - 20, kLogicalH - 20), ImGuiCond_Once);
        ImGui::Begin("城市贴图缩放调整", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::Text("密铺: %s (%d/%d)", lw::tilingName(it.tiling), batchIndex + 1,
                    static_cast<int>(itemsByBatch.size()));
        ImGui::Text("条目: %d/%d", itemIndex + 1,
                    static_cast<int>(itemsByBatch[static_cast<std::size_t>(batchIndex)].size()));
        ImGui::Text("等级: %.4g  贴图等级: %d", it.level, it.iconLevel);
        ImGui::Text("形状下标: %d  变体: %d", it.shapeIndex, it.variantIndex);
        ImGui::Text("锚朝向: %s", it.anchorDesc.c_str());
        ImGui::Text("几何中心: (%.3f, %.3f)", view.centerX, view.centerY);
        ImGui::Text("自动计算建议: 缩放 %.4f, 平移 %.4f", view.autoScale, view.autoOffsetY);
        ImGui::Separator();

        if (ImGui::Button("上一批")) changeBatch(-1);
        ImGui::SameLine();
        if (ImGui::Button("下一批")) changeBatch(1);
        if (ImGui::Button("上一个")) changeItem(-1);
        ImGui::SameLine();
        if (ImGui::Button("下一个")) changeItem(1);

        if (ImGui::SliderFloat("缩放倍率", &sliderScale, 0.0f, sliderMax, "%.4f")) {
            currentSaved = false;
            dirty = true;
        }
        if (ImGui::SliderFloat("竖直平移(世界单位, 正=上)", &sliderOffsetY, -2.0f, 2.0f, "%.4f")) {
            currentSaved = false;
            dirty = true;
        }

        if (!hasLastSaved) ImGui::BeginDisabled();
        if (ImGui::Button("使用上一个的数值")) {
            sliderScale = lastSavedScale;
            sliderOffsetY = lastSavedOffsetY;
            currentSaved = false;
            dirty = true;
        }
        if (!hasLastSaved) ImGui::EndDisabled();
        if (hasLastSaved)
            ImGui::Text("上一个保存: 缩放 %.4f, 平移 %.4f", lastSavedScale, lastSavedOffsetY);

        ImGui::Separator();
        ImGui::Text("暂存区");
        for (int i = 0; i < kSlotCount; ++i) {
            ImGui::PushID(i);
            if (slotUsed[static_cast<std::size_t>(i)])
                ImGui::Text("暂存%d: 缩放 %.4f 平移 %.4f", i + 1,
                            slotScale[static_cast<std::size_t>(i)],
                            slotOffsetY[static_cast<std::size_t>(i)]);
            else
                ImGui::Text("暂存%d: 空", i + 1);
            if (ImGui::Button("写入")) {
                slotScale[static_cast<std::size_t>(i)] = sliderScale;
                slotOffsetY[static_cast<std::size_t>(i)] = sliderOffsetY;
                slotUsed[static_cast<std::size_t>(i)] = true;
                saveJsonFile(itemsByBatch, savedScale, savedOffsetY, lastSavedScale,
                             lastSavedOffsetY, hasLastSaved, slotScale, slotOffsetY, slotUsed);
            }
            ImGui::SameLine();
            if (!slotUsed[static_cast<std::size_t>(i)]) ImGui::BeginDisabled();
            if (ImGui::Button("读出")) {
                sliderScale = slotScale[static_cast<std::size_t>(i)];
                sliderOffsetY = slotOffsetY[static_cast<std::size_t>(i)];
                currentSaved = false;
                dirty = true;
            }
            if (!slotUsed[static_cast<std::size_t>(i)]) ImGui::EndDisabled();
            ImGui::PopID();
        }

        if (ImGui::Button("保存当前条目")) {
            const std::string key = itemKey(it);
            savedScale[key] = sliderScale;
            savedOffsetY[key] = sliderOffsetY;
            lastSavedScale = sliderScale;
            lastSavedOffsetY = sliderOffsetY;
            hasLastSaved = true;
            saveJsonFile(itemsByBatch, savedScale, savedOffsetY, lastSavedScale, lastSavedOffsetY,
                         hasLastSaved, slotScale, slotOffsetY, slotUsed);
            currentSaved = true;
            dirty = false;
        }
        ImGui::Text(currentSaved ? "当前条目已保存" : "当前条目尚未保存");
        ImGui::Text("临时文件: %s", kSavePath);
        ImGui::End();

        lw::ui::renderImGui(ren);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    // 清理。
    for (auto* tex : texs)
        if (tex) SDL_DestroyTexture(tex);
    lw::ui::shutdownImGui();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
