// EffectRenderer.cpp — 特效渲染实现（翻新计划 Phase 7，§2.9；Phase 8 接入 Camera）。
#include "render/EffectRenderer.h"

#include <cmath>

#include "core/GameDefs.h"
#include "core/Simulation.h"
#include "render/RendererUtil.h"
#include "sim/components.h"

namespace lw::render {

namespace {
constexpr int kSpriteSize = 32;  // 激光火花原尺寸（scale 1）
}  // namespace

EffectRenderer::EffectRenderer(SDL_Renderer* ren, const SpriteSheet& sheet, const Camera& cam,
                               int mineDrawSize, const std::vector<std::array<int, 3>>& primaryColors)
    : ren_(ren), sheet_(sheet), cam_(cam), mineDrawSize_(mineDrawSize),
      primaryColors_(primaryColors) {}

void EffectRenderer::draw(const Simulation& sim) {
    const auto& reg = sim.registry();
    Renderer r(ren_);
    const std::uint64_t simTick = sim.tickCount();
    const double z = cam_.zoom();

    for (auto e : reg.view<comp::EffectTypeId>()) {
        const auto& pos = reg.get<comp::Position>(e);
        const int fid = reg.get<comp::FactionId>(e).value;
        if (fid < 1 || fid >= sim.factionCount()) continue;
        const auto& params = reg.get<comp::EffectParams>(e);
        const int elapsedTicks =
            static_cast<int>(simTick) - reg.get<comp::EffectTimer>(e).createdTick;
        // 视野剔除（2026-08 工程改进）：按特效类型取保守世界半径（爆炸动态增长、
        // 激光取光束长），离屏则跳过。
        const EffectType et = reg.get<comp::EffectTypeId>(e).type;
        const double worldRadius =
            (et == EffectType::bomb)
                ? std::sqrt(static_cast<double>(elapsedTicks)
                                * sim.config().effect.bomb.expansionRate
                            + 1.0)
                      * params.p0
                    + 1.0
                : ((et == EffectType::laser) ? params.p1 + 2.0 : 2.0);
        if (!render::isVisibleOnScreen(cam_, pos.x, pos.y, worldRadius)) continue;
        const int cx = cam_.toScreenXi(pos.x);
        const int cy = cam_.toScreenYi(pos.y);
        // 爆炸圆等直接色填充：优先调色板主色（双色系统，视觉工程改进 ⑫），缺省回退 sim 势力色。
        const std::array<int, 3> color =
            (fid >= 1 && fid < sim.factionCount() &&
             static_cast<std::size_t>(fid - 1) < primaryColors_.size())
                ? primaryColors_[static_cast<std::size_t>(fid - 1)]
                : sim.faction(fid).color;

        switch (et) {
            case EffectType::bomb: {
                // 爆炸：半透明实心圆，与 EffectSystem 使用同一扩散公式。
                const auto& bombCfg = sim.config().effect.bomb;
                if (elapsedTicks < 0 || elapsedTicks > bombCfg.lifetimeTicks) break;
                const double rWorld =
                    std::sqrt(static_cast<double>(elapsedTicks) * bombCfg.expansionRate + 1.0)
                    * params.p0;
                const int alpha = static_cast<int>(
                    (static_cast<double>(bombCfg.lifetimeTicks + 1) - elapsedTicks)
                    / static_cast<double>(bombCfg.lifetimeTicks + 1) * 255.0);
                r.fillCircle(cx, cy, static_cast<int>(rWorld * cam_.cellPx()),
                             Renderer::toColor(color, alpha));
                break;
            }
            case EffectType::mine: {
                // 地雷：进入可引爆状态后，在每个探测周期的首尾短暂增强亮度。
                const auto& mineCfg = sim.config().effect.mine;
                const int period = mineCfg.checkEveryTicks;
                const int mod = ((elapsedTicks % period) + period) % period;
                const bool blink = elapsedTicks >= mineCfg.armTicks
                                   && (mod <= 2 || mod >= period - 3);
                const int size = render::spriteSize(mineDrawSize_, z);
                // 双色渲染（视觉工程改进 ⑫）：统一 army_base.png 行 5。
                SDL_Texture* tex = sheet_.texture(fid - 1);
                r.drawSpriteCentered(tex, sheet_.rect(5), cx, cy, size, blink ? 220 : 60);
                break;
            }
            case EffectType::laser: {
                // 光束：第 7 行子图 DrawRotaGraph3 等价。p0=世界角，p1=本 tick 光束长。
                // dst 宽 16*z（0.5×32 随缩放）、高 cellPx*finalLength；pivot=顶中点(8*z,0)。
                // SDL 正角=顺时针（与 DxLib 一致），角 = -angle-π/2（§2.9）。
                // 曾试 ADD 混合（发光风格），但本机 direct3d 渲染器在 logical-size 下 ADD
                // 静默失效（RenderCopyEx 返回 0 无错误却什么都不画，探针+实测确认）→ 回退 BLEND。
                // 改用分层解决"势力色描边进白色头部"：光束先画、火花（BLEND）盖在其上。
                const double angle = params.p0;
                const int h = static_cast<int>(std::lround(cam_.cellPx() * params.p1));
                if (h > 0) {
                    const int w = std::max(2, static_cast<int>(std::lround(16.0 * z)));
                    const int pv = static_cast<int>(std::lround(8.0 * z));
                    const double angleDeg = (-angle - kPi / 2.0) * 180.0 / kPi;
                    r.drawSpriteRotated(sheet_.texture(fid - 1), sheet_.rect(7), cx - pv, cy, w, h,
                                        angleDeg, pv, 0);
                }
                // 火花：第 6 行子图，旋转角 = (sim tickCount, 实体 id) 的确定性哈希
                // （Phase 9：暂停时 ttime 冻结 → 火花静态；运行中每 tick 换角，视觉随机，
                // 不碰 sim RNG → 确定性/回放成立）。BLEND 画在光束之上（头实束虚）。
                const std::uint64_t rotSeed =
                    static_cast<std::uint64_t>(simTick) * 1103515245ull
                    + static_cast<std::uint32_t>(e) * 2654435761ull;
                const double rotDeg = static_cast<double>((rotSeed >> 16) % 360);
                const int sp = render::spriteSize(kSpriteSize, z);
                r.drawSpriteRotated(sheet_.texture(fid - 1), sheet_.rect(6), cx - sp / 2,
                                    cy - sp / 2, sp, sp, rotDeg, sp / 2, sp / 2);
                break;
            }
        }
    }
}

}  // namespace lw::render
