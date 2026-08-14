// TwoToneTintCache.h — 双色兵表烘焙（视觉工程改进 ⑫）。
// 读取源图（army_base.png，红势力成品渲染）→ 逐像素反解双色模板 {v,m,Δ,α}（RGBA8 表面：
// R=v、G=m、B=Δ、A=α）→ 按各势力 (主色, 副色) 对**全分辨率软件合成** RGBA → 建纹理。
// 运行时每兵仍单次 drawSpriteCentered（零额外 draw call）；模板与合成均纯逻辑可单测
// （TintMath.h）。调试：模板导出 userdata/two_tone_template.png（肉眼检查反解质量）。
// 无 LOD 档位（兵表维持单档 32px，与改造前一致）。
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

namespace lw::render {

class TwoToneTintCache {
public:
    TwoToneTintCache() = default;
    ~TwoToneTintCache();
    TwoToneTintCache(const TwoToneTintCache&) = delete;
    TwoToneTintCache& operator=(const TwoToneTintCache&) = delete;

    // 加载 path 源图，按 pairs（第 i 个势力的 (主色, 副色)）烘焙 i 份合成纹理。
    // 失败返回 false（错误经 spdlog 记录，并清空已有纹理）。
    bool load(SDL_Renderer* ren, const std::string& path,
              const std::vector<std::pair<std::array<int, 3>, std::array<int, 3>>>& pairs);

    // 第 index 份合成纹理（越界/未加载 → nullptr）。
    SDL_Texture* texture(int index) const;
    int count() const { return count_; }
    int width() const { return w_; }    // 源图宽
    int height() const { return h_; }   // 源图高

    // 显式销毁全部纹理（可重复调用；析构也会调用）。
    void release();

private:
    std::vector<SDL_Texture*> textures_;
    int count_ = 0;
    int w_ = 0;
    int h_ = 0;
};

}  // namespace lw::render
