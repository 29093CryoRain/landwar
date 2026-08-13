// MapRenderer.cpp — 地图网格渲染实现（翻新计划 Phase 7 §2.9；P5 山地/城市改版）。
#include "render/MapRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "core/GameDefs.h"

namespace lw::render {

namespace {
constexpr unsigned kWhite = 0xFFFFFFu;  // RGB_COLOR(255,255,255)
constexpr unsigned kBlack = 0x000000u;  // RGB_COLOR(0,0,0)
constexpr int kTerrainTexSize = 128;    // data/mountain.png、data/city.png 源图尺寸
// 颜色组数 = 势力数(9，含中立0) —— 陆地基色按同色格聚合的批次上限。
constexpr int kColorGroups = kPlayerFactionCount + 1;  // 9

// 单通道按比例缩放（mix_color(色,黑,rate) = 色×rate，黑项恒 0）。
std::array<int, 3> scaled(const std::array<int, 3>& col, double rate) {
    std::array<int, 3> out;
    for (int i = 0; i < 3; ++i)
        out[static_cast<size_t>(i)] =
            static_cast<int>(col[static_cast<size_t>(i)] * rate + 0.5);
    return out;
}

// LOD 档位选择：选预烘焙尺寸与屏上格大小最近的档（同距优先大档 → 尽量降采样而非放大，
// 避免小档上采样发糊）。屏上格越小 → 档位越小，规避 128px 线条图被 GPU 糊成几像素。
int pickSizeIndex(const TintCache& cache, int target) {
    int best = 0, bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < cache.sizeCount(); ++i) {
        const int d = std::abs(cache.sizePixel(i) - target);
        if (d < bestDist) {  // 严格小于 → 平局保留先出现的（更大档位）
            bestDist = d;
            best = i;
        }
    }
    return best;
}
}  // namespace

void MapRenderer::bake(const std::vector<std::array<int, 3>>& factionColors) {
    // 白底透明线条源图 → 目标色（Multiply 整体 tint，同玩家指示贴图管线）。
    // 山 = 近黑但不纯黑（8% 势力色）。计划草案示例数值（山 0.92）与 mixColor 语义
    // （rate=势力色占比）相抵触，按文字描述实现（近黑），已记入开发计划 P5 完成记录。
    // P13：城贴图迁出到 CityRenderer（基建格/等级图标/细线围区），此处只烘焙山。
    std::vector<std::array<int, 3>> mtn;
    mtn.reserve(factionColors.size());
    for (const auto& c : factionColors) mtn.push_back(scaled(c, 0.08));
    // LOD 多尺寸（细节改进 2026-08）：预烘焙 128/64/32/16 档，缩放小时按屏上格大小选档，
    // 规避 128px 线条图被 GPU 降采样糊成几像素（见 drawOverlay 的 pickSizeIndex）。
    const std::vector<int> lodSizes = {kTerrainTexSize, 64, 32, 16};
    mountain_.load(ren_, "data/mountain.png", mtn, TintMode::Multiply, /*grayscaleFirst=*/false,
                   /*preserveWhite=*/0, lodSizes);
}

void MapRenderer::draw(const Map& map, const std::vector<Faction>& factions) {
    // 陆地基色（山/城同底色）按势力聚合矩形批次 → 一次 SDL_RenderFillRects 画一组。
    std::array<std::vector<SDL_Rect>, kColorGroups> batches;
    std::array<SDL_Color, kColorGroups> groupColor;
    for (int f = 0; f < kColorGroups; ++f) {
        const auto& col = factions[static_cast<size_t>(f)].color;
        groupColor[static_cast<size_t>(f)] = Renderer::mixed(col, kWhite, 0.6);  // 陆地
    }
    Renderer r(ren_);
    // 视野剔除（缩放/平移时只绘制可见范围，避免扫描全图 9975 格）。
    const int i0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldX0())), 0, map.width() - 1);
    const int i1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldX1())), 0, map.width() - 1);
    const int j0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldY0())), 0, map.height() - 1);
    const int j1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldY1())), 0, map.height() - 1);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (!cell.land) continue;  // 海 = 背景黑（原版零宽空操作）
            const int g = std::clamp(static_cast<int>(cell.belongi), 0, kColorGroups - 1);
            batches[static_cast<size_t>(g)].push_back(cam_.cellRect(i, j));
        }
    }
    for (int g = 0; g < kColorGroups; ++g) {
        if (!batches[static_cast<size_t>(g)].empty())
            r.fillRects(batches[static_cast<size_t>(g)], groupColor[static_cast<size_t>(g)]);
    }

    // 叠加山线条贴图（P13：城市贴图迁出到 CityRenderer，本层只画山）。
    if (mountain_.count() == 0) return;  // 贴图缺失/未烘焙 → 跳过叠加（保底可运行）
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (!cell.land || !cell.mountain) continue;
            const SDL_Rect rc = cam_.cellRect(i, j);
            const int size = std::max(rc.w, rc.h);
            if (size <= 0) continue;
            const int ci = std::clamp(static_cast<int>(cell.belongi), 0, mountain_.count() - 1);
            const int si = pickSizeIndex(mountain_, size);  // LOD：按屏上格大小选预烘焙档位
            SDL_Texture* tex = mountain_.texture(ci, si);
            if (!tex) continue;
            const int srcSize = mountain_.sizePixel(si);
            r.drawSpriteCentered(tex, {0, 0, srcSize, srcSize}, rc.x + rc.w / 2, rc.y + rc.h / 2,
                                 size);
        }
    }
}

}  // namespace lw::render
