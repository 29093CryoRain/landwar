// EffectRenderer.h — 特效渲染（翻新计划 Phase 7，§3.1 render/；规格 §2.9）。
// 三种特效：
//   bomb  — 半透明实心圆，r = sqrt(tt+1)*bomb_range（按 §2.9 注记用 solve 版公式），
//           alpha = (9-tt)/9*255，填充势力色。
//   mine  — army.png 第 5 行（type=5）子图 48×48；tt>=600 且 tt%12∈{0,1,2,10,11} → alpha 220，
//           否则 60（闪烁）。
//   laser — 火花：第 6 行子图 32×32 居中旋转，旋转角 = (sim tickCount, 实体 id) 的确定性
//           哈希（Phase 9：暂停时 ttime 冻结 → 火花静态；运行中每 tick 换角，视觉随机，
//           不碰 sim RNG）；光束：第 7 行子图 DrawRotaGraph3 等价
//           （pivot=顶中点，dst 宽 16 / 高 15*finalLength，角 -angle-π/2）。
//           **分层绘制**：光束先画、火花（BLEND）盖在其上 → 白色头部盖住光束顶缘的势力色
//           描边（原版 alpha 混合下描边会压进白色火头内部，见 §2.9 注记）。曾试 ADD 发光
//           风格，但本机 direct3d 渲染器在 logical-size 下 ADD 静默失效（RenderCopyEx 返回
//           0 无错误却什么都不画）→ 回退 BLEND，仅靠分层解决。绿势力三束同源，按 entity
//           创建序（侧1→侧2→中心）迭代 → 中心束最后画，天然在上层（交汇处基本被火花遮盖）。
#pragma once

#include <SDL.h>

#include <array>
#include <vector>

#include "core/GameDefs.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/SpriteSheet.h"

namespace lw {

class Simulation;  // 前向声明

namespace render {

class EffectRenderer {
public:
    EffectRenderer(SDL_Renderer* ren, const SpriteSheet& sheet, const Camera& cam,
                   int mineDrawSize = 48,
                   const std::vector<std::array<int, 3>>& primaryColors = {});

    // 绘制全部特效。tt = sim.tickCount() - createdTick。
    void draw(const Simulation& sim);

    // F5/F7 重烘焙：更新主色表（爆炸圆等直接色填充；视觉工程改进 ⑫）。
    void reloadColors(const std::vector<std::array<int, 3>>& primaryColors) {
        primaryColors_ = primaryColors;
    }

private:
    SDL_Renderer* ren_;
    const SpriteSheet& sheet_;
    const Camera& cam_;
    int mineDrawSize_;  // 地雷 sprite 绘制尺寸（config.render.armyDrawSize）
    // 势力主色表（下标 = 势力 id-1；双色调色板，视觉工程改进 ⑫——爆炸圆等直接色填充用；
    // 缺省时回退 sim 势力色，保证单测/旧构造可用）。
    std::vector<std::array<int, 3>> primaryColors_;
};

}  // namespace lw::render
}  // namespace lw
