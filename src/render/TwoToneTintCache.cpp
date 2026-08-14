// TwoToneTintCache.cpp — 双色兵表烘焙实现（视觉工程改进 ⑫）。
#include "render/TwoToneTintCache.h"

#include <SDL_image.h>
#include <spdlog/spdlog.h>

#include "core/Paths.h"
#include "render/TintMath.h"

namespace lw::render {

TwoToneTintCache::~TwoToneTintCache() {
    release();
}

void TwoToneTintCache::release() {
    for (SDL_Texture* t : textures_) {
        if (t) SDL_DestroyTexture(t);
    }
    textures_.clear();
    count_ = 0;
    w_ = h_ = 0;
}

bool TwoToneTintCache::load(
    SDL_Renderer* ren, const std::string& path,
    const std::vector<std::pair<std::array<int, 3>, std::array<int, 3>>>& pairs) {
    release();
    if (!ren) {
        spdlog::error("TwoToneTintCache: load('{}') with null renderer", path);
        return false;
    }

    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) {
        spdlog::error("TwoToneTintCache: IMG_Load('{}') failed: {}", path, IMG_GetError());
        return false;
    }
    SDL_Surface* src = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!src) {
        spdlog::error("TwoToneTintCache: surface convert '{}' failed: {}", path, SDL_GetError());
        return false;
    }
    w_ = src->w;
    h_ = src->h;
    SDL_PixelFormat* fmt = src->format;
    const Uint32* srcPx = static_cast<Uint32*>(src->pixels);

    // 模板表面（R=v、G=m、B=Δ、A=α）。一次反解，供全部势力合成复用。
    SDL_Surface* tmpl =
        SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!tmpl) {
        spdlog::error("TwoToneTintCache: template surface create failed: {}", SDL_GetError());
        SDL_FreeSurface(src);
        return false;
    }
    Uint32* tp = static_cast<Uint32*>(tmpl->pixels);
    for (int y = 0; y < src->h; ++y) {
        for (int x = 0; x < src->w; ++x) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            SDL_GetRGBA(srcPx[static_cast<size_t>(y) * src->w + x], fmt, &r, &g, &b, &a);
            const TwoToneTemplate t = decomposeTwoTonePixel(r, g, b, a);
            tp[static_cast<size_t>(y) * src->w + x] =
                SDL_MapRGBA(tmpl->format, static_cast<Uint8>(t.v), static_cast<Uint8>(t.m),
                            static_cast<Uint8>(t.delta), static_cast<Uint8>(t.a));
        }
    }
    // 调试：模板导出 userdata/（反解质量肉眼检查；失败仅记日志，不阻断）。
    if (ensureDirExists(kUserDataDir)) {
        if (IMG_SavePNG(tmpl, "userdata/two_tone_template.png") != 0)
            spdlog::warn("TwoToneTintCache: template dump failed: {}", IMG_GetError());
    }

    // 每势力对：全分辨率合成 → 建纹理。
    count_ = static_cast<int>(pairs.size());
    bool ok = true;
    for (const auto& pr : pairs) {
        SDL_Surface* out =
            SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!out) {
            spdlog::error("TwoToneTintCache: out surface create failed: {}", SDL_GetError());
            ok = false;
            break;
        }
        Uint32* dp = static_cast<Uint32*>(out->pixels);
        for (int y = 0; y < src->h; ++y) {
            for (int x = 0; x < src->w; ++x) {
                Uint8 tv = 0, tm = 0, td = 0, ta = 0;
                SDL_GetRGBA(tp[static_cast<size_t>(y) * src->w + x], tmpl->format, &tv, &tm, &td,
                            &ta);
                const TintRgba c = compositeTwoTone(TwoToneTemplate{tv, tm, td, ta}, pr.first,
                                                    pr.second);
                dp[static_cast<size_t>(y) * src->w + x] =
                    SDL_MapRGBA(out->format, static_cast<Uint8>(c.r), static_cast<Uint8>(c.g),
                                static_cast<Uint8>(c.b), static_cast<Uint8>(c.a));
            }
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, out);
        SDL_FreeSurface(out);
        if (!tex) {
            spdlog::error("TwoToneTintCache: texture create failed: {}", SDL_GetError());
            ok = false;
            break;
        }
        textures_.push_back(tex);
    }

    SDL_FreeSurface(tmpl);
    SDL_FreeSurface(src);
    if (!ok) {
        release();
        return false;
    }
    spdlog::info("TwoToneTintCache: '{}' baked {} two-tone variant(s) ({}x{})", path, count_, w_,
                 h_);
    return true;
}

SDL_Texture* TwoToneTintCache::texture(int index) const {
    if (index < 0 || index >= count_) return nullptr;
    return textures_[static_cast<size_t>(index)];
}

}  // namespace lw::render
