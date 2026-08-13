// TintCache.h — 读入源图 → 按一组目标色烘焙多份"单色 tint"纹理（存内存）。
// 统一贴图管线（开发计划：读图→单色 tint→多版本存内存）：
//   - 玩家指示（白底箭头/旋转环 → 8 势力色）；
//   - 军队精灵（灰度源 → 势力色 × 亮度）；
//   - 地形线条贴图（P5 山/城、P9 子弹等）统一走本通道。
// 逐像素调用 TintMath::tintPixel（grayscaleFirst / preserveWhiteAbove）。
// 渲染器在 load() 传入（成员默认构造，可被 SpriteSheet 等持有）。
//
// **多尺寸 LOD（2026-08-06，细节改进）**：load() 可传 sizes 档位（含源图尺寸），
// 烘焙时对源图尺寸之外的档位做预乘 alpha 双线性降采样 → 缩放小时用预烘焙小档位，
// 避免 128→几像素的 GPU 线性/最近降采样糊成一团。纹理存 [color × size] 平铺；
// sizeIndex 0 = 源图尺寸（texture(colorIndex) 向后兼容返回它）。
// **非方形源图（2026-08-07 城市塔修复）**：各档位按源图纵横比降采样（宽=s、高=round(s×h/w)），
// 不强制压成方形——否则塔图（如 69×145）被压成方形 LOD，绘制时 src 又按源图尺寸采样，
// 内容被挤到目标矩形左上角（明显偏移）。width()/height(sizeIndex) 返回该档实际像素尺寸。
#pragma once

#include <array>
#include <string>
#include <vector>

#include <SDL.h>

namespace lw::render {

// 着色模式：
//   Multiply  — tintPixel：白/灰源 → 目标色（玩家标记用；grayscaleFirst/preserveWhite 可用）。
//   HueRotate — hueRotatePixel：彩色源按色相旋转到目标色，delta=hue(colors[i])-hue(colors[0])
//               （军队精灵用：精确复现原图色相处理，保留明度/饱和度，白像素不动）。
enum class TintMode { Multiply, HueRotate };

class TintCache {
public:
    TintCache() = default;
    ~TintCache();
    TintCache(const TintCache&) = delete;
    TintCache& operator=(const TintCache&) = delete;

    // 加载 path，烘焙 colors.size() 份目标色版本（ren 为渲染器）。sizes 指定烘焙档位
    // （如 {128, 64, 32, 16}，须含源图尺寸；> 源图尺寸的档位被忽略，不放大）；为空 →
    // 仅源图尺寸（向后兼容）。失败返回 false（错误经 spdlog 记录，并清空已有纹理）。
    bool load(SDL_Renderer* ren, const std::string& path,
              const std::vector<std::array<int, 3>>& colors, TintMode mode = TintMode::Multiply,
              bool grayscaleFirst = false, int preserveWhiteAbove = 0,
              std::vector<int> sizes = {});

    // 取第 colorIndex 份着色、第 sizeIndex 档纹理（sizeIndex 0 = 源图尺寸；越界/未加载 → nullptr）。
    SDL_Texture* texture(int colorIndex, int sizeIndex) const;
    // 取第 colorIndex 份着色纹理的源图尺寸版（向后兼容）。
    SDL_Texture* texture(int colorIndex) const;
    int count() const { return colorCount_; }                 // 颜色数（势力数）
    int sizeCount() const { return sizes_.empty() ? 1 : static_cast<int>(sizes_.size()); }
    int sizePixel(int sizeIndex) const;                       // 第 sizeIndex 档的标称宽（0=源图宽）
    int width(int sizeIndex) const;                           // 第 sizeIndex 档实际宽（= sizePixel）
    int height(int sizeIndex) const;                          // 第 sizeIndex 档实际高（按源纵横比）
    int width() const { return texW_; }                       // 源图宽（向后兼容）
    int height() const { return texH_; }                      // 源图高

    // LOD 档位选择（2026-08 工程改进，从 MapRenderer/CityRenderer 各副本收敛到本类）：
    // 选预烘焙尺寸与 target 屏上尺寸最近的档（同距优先大档 → 尽量降采样而非放大，
    // 避免小档上采样发糊）。target <= 0 → 返回 0（源图档）。
    int pickSizeIndex(int target) const;

    // 显式销毁全部纹理（须先于 SDL_DestroyRenderer；析构也会调用，可重复调用）。
    void release();

private:
    std::vector<SDL_Texture*> textures_;  // [colorIndex * sizeCount + sizeIndex]
    std::vector<int> sizes_;              // 降序（首项 = 源图尺寸）
    int colorCount_ = 0;                  // 颜色数（= count()）
    int texW_ = 0;                        // 源图宽
    int texH_ = 0;                        // 源图高
};

}  // namespace lw::render
