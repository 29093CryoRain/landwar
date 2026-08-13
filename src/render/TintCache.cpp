// TintCache.cpp — 多版本 tint 纹理烘焙实现。
#include "render/TintCache.h"

#include <SDL_image.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "render/TintMath.h"

namespace lw::render {

namespace {

// 双线性降采样（**预乘 alpha 插值**：r/g/b 先 ×a 再平均，最后 ÷a——避免透明边缘的 RGB
// 残留渗入不透明区，保证半透明边缘颜色正确）。src 须为 RGBA32 surface；目标尺寸等于源图
// 时原样返回 src（不新建）。返回的 surface 由调用方释放（等于 src 时不要释放 src 外部已持有）。
SDL_Surface* scaleSurface(SDL_Surface* src, int dstW, int dstH) {
    if (src->w == dstW && src->h == dstH) return src;
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, dstW, dstH, 32, SDL_PIXELFORMAT_RGBA32);
    if (!out) return nullptr;
    const Uint32* sp = static_cast<Uint32*>(src->pixels);
    Uint32* dp = static_cast<Uint32*>(out->pixels);
    const int sw = src->w, sh = src->h;
    SDL_PixelFormat* fmt = src->format;
    // 采样 (px,py) 像素的预乘 rgba。
    const auto samplePMA = [&](int px, int py) -> std::array<double, 4> {
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_GetRGBA(sp[static_cast<size_t>(py) * sw + px], fmt, &r, &g, &b, &a);
        const double da = a;
        return {r * da, g * da, b * da, da};
    };
    const auto clamp255 = [](double v) -> Uint8 {
        return static_cast<Uint8>(v < 0.0 ? 0 : (v > 255.0 ? 255.0 : v));
    };
    for (int y = 0; y < dstH; ++y) {
        const double sy = (y + 0.5) * sh / dstH - 0.5;
        const int y0 = std::max(0, static_cast<int>(std::floor(sy)));
        const int y1 = std::min(sh - 1, y0 + 1);
        const double fy = sy - y0;
        for (int x = 0; x < dstW; ++x) {
            const double sx = (x + 0.5) * sw / dstW - 0.5;
            const int x0 = std::max(0, static_cast<int>(std::floor(sx)));
            const int x1 = std::min(sw - 1, x0 + 1);
            const double fx = sx - x0;
            const auto p00 = samplePMA(x0, y0), p10 = samplePMA(x1, y0);
            const auto p01 = samplePMA(x0, y1), p11 = samplePMA(x1, y1);
            const double a = (1 - fx) * (1 - fy) * p00[3] + fx * (1 - fy) * p10[3]
                             + (1 - fx) * fy * p01[3] + fx * fy * p11[3];
            double r = (1 - fx) * (1 - fy) * p00[0] + fx * (1 - fy) * p10[0]
                       + (1 - fx) * fy * p01[0] + fx * fy * p11[0];
            double g = (1 - fx) * (1 - fy) * p00[1] + fx * (1 - fy) * p10[1]
                       + (1 - fx) * fy * p01[1] + fx * fy * p11[1];
            double b = (1 - fx) * (1 - fy) * p00[2] + fx * (1 - fy) * p10[2]
                       + (1 - fx) * fy * p01[2] + fx * fy * p11[2];
            if (a > 1e-9) {
                r /= a;
                g /= a;
                b /= a;
            } else {
                r = g = b = 0;
            }
            dp[static_cast<size_t>(y) * dstW + x] =
                SDL_MapRGBA(out->format, clamp255(r), clamp255(g), clamp255(b), clamp255(a));
        }
    }
    return out;
}

}  // namespace

TintCache::~TintCache() {
    release();
}

void TintCache::release() {
    for (SDL_Texture* t : textures_) {
        if (t) SDL_DestroyTexture(t);
    }
    textures_.clear();
    sizes_.clear();
    colorCount_ = 0;
    texW_ = texH_ = 0;
}

bool TintCache::load(SDL_Renderer* ren, const std::string& path,
                     const std::vector<std::array<int, 3>>& colors, TintMode mode,
                     bool grayscaleFirst, int preserveWhiteAbove, std::vector<int> sizes) {
    release();
    if (!ren) {
        spdlog::error("TintCache: load('{}') with null renderer", path);
        return false;
    }

    // HueRotate：预先算每个目标色的色相偏移 delta = hue(colors[i]) - hue(colors[0])。
    // （colors[0] = 基准势力色，源图即其色相；如 army_base 源 = 势力1 红 → delta=该势力色相。）
    std::vector<double> deltas;
    if (mode == TintMode::HueRotate) {
        deltas.reserve(colors.size());
        const double baseHue =
            colors.empty() ? 0.0
                           : hueOf(colors[0][0], colors[0][1], colors[0][2]);
        for (const auto& c : colors)
            deltas.push_back(hueOf(c[0], c[1], c[2]) - baseHue);
    }

    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) {
        spdlog::error("TintCache: IMG_Load('{}') failed: {}", path, IMG_GetError());
        return false;
    }
    // 统一转 RGBA32，逐像素读取一致。
    SDL_Surface* src = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!src) {
        spdlog::error("TintCache: surface convert '{}' failed: {}", path, SDL_GetError());
        return false;
    }
    texW_ = src->w;
    texH_ = src->h;
    SDL_PixelFormat* fmt = src->format;
    const Uint32* srcPx = static_cast<Uint32*>(src->pixels);

    // 规范化档位：源图尺寸恒为最大档（sizeIndex 0，texture(colorIndex) 向后兼容）；>源图/≤0
    // 忽略（不放大）；其余降序去重。空 sizes → 仅源图尺寸（旧调用方行为不变）。
    sizes_.clear();
    sizes_.push_back(src->w);
    for (int s : sizes) {
        if (s > 0 && s < src->w) sizes_.push_back(s);
    }
    std::sort(sizes_.begin(), sizes_.end(), std::greater<int>());
    sizes_.erase(std::unique(sizes_.begin(), sizes_.end()), sizes_.end());
    colorCount_ = static_cast<int>(colors.size());

    bool ok = true;
    for (std::size_t ci = 0; ci < colors.size(); ++ci) {
        const auto& color = colors[ci];
        const double delta = (mode == TintMode::HueRotate && ci < deltas.size()) ? deltas[ci] : 0.0;
        // 先整幅源图尺寸 tint（一次），再对各档位降采样建纹理——避免每档重复 tint。
        SDL_Surface* base =
            SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!base) {
            spdlog::error("TintCache: out surface create failed: {}", SDL_GetError());
            ok = false;
            break;
        }
        Uint32* dstPx = static_cast<Uint32*>(base->pixels);
        for (int y = 0; y < src->h; ++y) {
            for (int x = 0; x < src->w; ++x) {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                SDL_GetRGBA(srcPx[static_cast<size_t>(y) * src->w + x], fmt, &r, &g, &b, &a);
                const TintRgba t = (mode == TintMode::HueRotate)
                                       ? hueRotatePixel(r, g, b, a, delta)
                                       : tintPixel(r, g, b, a, color, grayscaleFirst,
                                                   preserveWhiteAbove);
                dstPx[static_cast<size_t>(y) * src->w + x] =
                    SDL_MapRGBA(base->format, static_cast<Uint8>(t.r), static_cast<Uint8>(t.g),
                                static_cast<Uint8>(t.b), static_cast<Uint8>(t.a));
            }
        }
        // 源图尺寸直接用 base；其余双线性降采样后建纹理。各档位保留源纵横比：
        // 宽 = s、高 = round(s × srcH/srcW)——非方形源图不压成方形（2026-08-07 城市塔修复）。
        for (int s : sizes_) {
            const bool isSrc = (s == src->w);
            const int bh = isSrc ? src->h
                                 : std::max(1, static_cast<int>(std::lround(
                                                   static_cast<double>(s) * src->h / src->w)));
            SDL_Surface* surf = isSrc ? base : scaleSurface(base, s, bh);
            if (!surf) {
                spdlog::error("TintCache: scale to {}x{} failed", s, bh);
                ok = false;
                break;
            }
            SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
            if (surf != base) SDL_FreeSurface(surf);  // 降采样副本用完即释放
            if (!tex) {
                spdlog::error("TintCache: texture create failed: {}", SDL_GetError());
                ok = false;
                break;
            }
            textures_.push_back(tex);
        }
        SDL_FreeSurface(base);
        if (!ok) break;
    }

    SDL_FreeSurface(src);
    if (!ok) {
        release();  // 部分失败 → 整体清空
        return false;
    }
    spdlog::info("TintCache: '{}' baked {} color(s) x {} size(s) ({}x{}) [{}]", path,
                 colorCount_, sizeCount(), texW_, texH_,
                 mode == TintMode::HueRotate ? "hue" : "mul");
    return true;
}

SDL_Texture* TintCache::texture(int colorIndex, int sizeIndex) const {
    if (colorIndex < 0 || colorIndex >= colorCount_) return nullptr;
    if (sizeIndex < 0 || sizeIndex >= sizeCount()) return nullptr;
    const int idx = colorIndex * sizeCount() + sizeIndex;
    if (idx < 0 || idx >= static_cast<int>(textures_.size())) return nullptr;
    return textures_[static_cast<size_t>(idx)];
}

SDL_Texture* TintCache::texture(int colorIndex) const {
    return texture(colorIndex, 0);  // sizeIndex 0 = 源图尺寸（向后兼容）
}

int TintCache::sizePixel(int sizeIndex) const {
    if (sizes_.empty()) return texW_;
    if (sizeIndex < 0 || sizeIndex >= static_cast<int>(sizes_.size())) return texW_;
    return sizes_[static_cast<size_t>(sizeIndex)];
}

int TintCache::pickSizeIndex(int target) const {
    if (target <= 0) return 0;
    int best = 0, bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < sizeCount(); ++i) {
        const int d = std::abs(sizePixel(i) - target);
        if (d < bestDist) {  // 严格小于 → 平局保留先出现的（更大档位）
            bestDist = d;
            best = i;
        }
    }
    return best;
}

int TintCache::width(int sizeIndex) const {
    return sizePixel(sizeIndex);  // 标称宽 = 档位值
}

int TintCache::height(int sizeIndex) const {
    const int w = width(sizeIndex);
    if (texW_ <= 0) return w;
    // 与 load() 烘焙一致：按源纵横比推导实际高（sizeIndex 0 = 源图高）。
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(w) * texH_ / texW_)));
}

}  // namespace lw::render
