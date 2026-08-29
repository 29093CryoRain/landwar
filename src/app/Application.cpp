// Application.cpp — SDL 窗口应用实现（翻新计划 Phase 7/8）。
#include "app/Application.h"

#include <imgui.h>
#include <SDL_image.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "core/GameDefs.h"
#include "core/FixedTimestep.h"
#include "core/Paths.h"
#include "sim/Buff.h"  // buffValueText（科研弹窗数值文案）
#include "sim/components.h"
#include "ui/ImGuiSetup.h"
#include "ui/Menu.h"
#include "ui/UiScale.h"
#include "ui/panels/StatsPanel.h"  // P11：统计面板注册
#include "ui/panels/TechPanel.h"   // P8：科技面板注册
#include "world/MapGenerator.h"

namespace lw {

namespace {
constexpr double kZoomStep = 1.1;  // 每次滚轮缩放的倍率
}  // namespace

Application::Application(std::uint32_t seed, std::string configPath, std::string optionsPath,
                         bool skipMenu)
    // 初始化顺序 = 成员声明顺序（seed_/configPath_/optionsPath_/options_/uiConfig_/skipMenu_）。
    // 注意：options_/uiConfig_ 在 ctor 里读取 optionsPath_/configPath_，二者必须先于它们初始化。
    : seed_(seed),
      configPath_(std::move(configPath)),
      optionsPath_(std::move(optionsPath)),
      options_(Options::loadFromFile(optionsPath_.empty() ? kDefaultOptionsPath : optionsPath_)),
      // init() 需要窗口/相机尺寸，而模拟在菜单后才构建——用 configPath 加载的 Config 兜底
      // （--load 时 setPrebuilt 会用存档 config 覆盖）。
      uiConfig_(Config::loadFromFile(configPath_.empty() ? kDefaultConfigPath : configPath_)),
      skipMenu_(skipMenu) {}

void Application::setPrebuilt(Simulation sim) {
    sim_ = std::move(sim);
    prebuilt_ = true;
    simReady_ = true;
    skipMenu_ = true;        // 读档直接开始，不进菜单
    uiConfig_ = sim_.config();  // 窗口尺寸以存档 config 为准
}

Application::~Application() {
    // 清理顺序：渲染器组 → 精灵表纹理 → ImGui → SDL 渲染器/窗口。
    // 纹理是渲染器创建的，必须先于渲染器销毁；ImGui 后端的绘制数据依赖渲染器。
    mapRenderer_.reset();
    cityRenderer_.reset();  // P13：纹理是渲染器创建的，必须先于渲染器销毁
    armyRenderer_.reset();
    effectRenderer_.reset();
    cityMarker_.reset();  // 纹理是渲染器创建的，必须先于渲染器销毁
    sheet_.release();
    if (imguiReady_) {
        ui::shutdownImGui();
        imguiReady_ = false;
    }
    if (ren_) {
        SDL_DestroyRenderer(ren_);
        ren_ = nullptr;
    }
    if (win_) {
        SDL_DestroyWindow(win_);
        win_ = nullptr;
    }
    SDL_Quit();
}

bool Application::init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        spdlog::critical("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    // 2026-08 工程改进：运行期产物（options/随机图/截图）统一入 userdata/。
    if (!ensureUserDataDirs()) {
        spdlog::warn("userdata dirs unavailable; options/maps/screenshots may fail to save");
    }

    const auto& rcfg = uiConfig_.render;

    // 窗口尺寸：设计尺寸 2175×1425 超出多数显示器，窗口会延伸到屏外（用户只见中间）。
    // 初始窗口按当前显示器适配（保持设计纵横比），渲染逻辑尺寸仍固定 2175×1425，
    // SDL_RenderSetLogicalSize 会把整张图缩放显示 → 全图可见、拉伸不花屏。
    int dispW = 1920, dispH = 1080;  // 兜底（拿不到显示器信息时）
    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(0, &bounds) == 0) {
        dispW = bounds.w;
        dispH = bounds.h;
    }
    const double scale = std::min(
        1.0, std::min(0.9 * dispW / static_cast<double>(rcfg.windowWidth),
                      0.9 * dispH / static_cast<double>(rcfg.windowHeight)));
    const int winW = static_cast<int>(rcfg.windowWidth * scale);
    const int winH = static_cast<int>(rcfg.windowHeight * scale);

    win_ = SDL_CreateWindow("landwar", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win_) {
        spdlog::critical("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    // 请求 vsync：无上限帧率会烧满 CPU（每帧还要画 ~1 万地图格 fillRect），系统一旦有
    // 其他负载，帧时间抖动 → 固定步长 accumulator 追不上 → 游戏卡成慢动作。
    // 锁定到显示器刷新率后 1 帧 ≈ 1/60s，accumulator 天然同步，不再忙转。
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) {
        spdlog::warn("accelerated renderer (vsync) unavailable ({}), retry without vsync",
                     SDL_GetError());
        ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!ren_) {
        spdlog::warn("accelerated renderer unavailable ({}), falling back to software",
                     SDL_GetError());
        ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren_) {
        spdlog::critical("SDL_CreateRenderer failed: {}", SDL_GetError());
        return false;
    }
    // 确认 vsync 是否真的生效（SDL 2.0.18+：返回 0 = 生效；软件兜底/驱动不支持时返回 -1）。
    vsync_ = (SDL_RenderSetVSync(ren_, 1) == 0);
    if (vsync_) {
        spdlog::info("vsync enabled (frame rate locked to display refresh)");
    } else {
        spdlog::warn("vsync unavailable, will use SDL_Delay frame limiter");
    }
    // 全帧合成开启 alpha 混合（半透明爆炸圆/地雷闪烁/兵 sprite 都需要）。
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    // 逻辑尺寸固定为窗口设计尺寸：窗口可拉伸缩放，渲染空间不变（缩放下不花屏，见验收）。
    SDL_RenderSetLogicalSize(ren_, rcfg.windowWidth, rcfg.windowHeight);
    spdlog::info("window {}x{} created (display {}x{}, scale {:.3f})", winW, winH, dispW, dispH,
                 scale);

    // 屏幕坐标变换（toscreenx/y，y 翻转）来自 config；相机（缩放/平移）基于它。
    // P12：密铺世界范围（六/三角非整数；方 = 格数）由 config tiling + 尺寸推导。
    const auto& mcfg = uiConfig_.map;
    const TilingGeom tg{tilingFromName(mcfg.tiling), mcfg.width, mcfg.height};
    tf_ = math::ScreenTransform{static_cast<double>(mcfg.blockSize), mcfg.panelWidth,
                                tg.worldHeight()};
    camera_.configure(tf_, rcfg.windowWidth, rcfg.windowHeight, tg.worldWidth(), tg.worldHeight());

    // 双色势力渲染（视觉工程改进 ⑫）：主副色与混合比例全部来自 config.json
    //（factions[i].color/secondary + render.tile / render.city.mix；F7 重烘焙肉眼评美）。

    mapRenderer_ = std::make_unique<render::MapRenderer>(ren_, camera_);
    // P13 城市渲染：基建格贴图 + 等级图标 + 高缩放细线围区（在 MapRenderer 之后绘制）。
    cityRenderer_ = std::make_unique<render::CityRenderer>(ren_, camera_);
    armyRenderer_ = std::make_unique<render::ArmyRenderer>(ren_, sheet_, camera_,
                                                           rcfg.armyDrawSize);
    // 特效/玩家指示构造时传空主色表，随后 applyPaletteColors() 统一填充（爆炸圆/环/箭头）。
    effectRenderer_ =
        std::make_unique<render::EffectRenderer>(ren_, sheet_, camera_, rcfg.armyDrawSize);
    projectileRenderer_ = std::make_unique<render::ProjectileRenderer>(
        ren_, sheet_, camera_, uiConfig_.projectile.drawSize);
    cityMarker_ = std::make_unique<render::CityMarkerRenderer>(
        ren_, camera_, std::vector<std::array<int, 3>>{});
    // 统一重烘焙：兵表（主副色对）+ 山/城/标记/特效 + 地块填色缓存。
    if (!applyPaletteColors()) {
        spdlog::critical("palette bake failed (sprite sheet load?)");
        return false;
    }

    if (!ui::initImGui(win_, ren_)) {
        spdlog::critical("ImGui init failed");
        return false;
    }
    imguiReady_ = true;

    setupPanels();  // P3：注册主面板 + 可移动面板，恢复 options.panels 布局
    return true;
}

void Application::processEvents() {
    // ImGui 是否想捕获鼠标（焦点在 ImGui 窗口上）→ 抑制游戏拖拽/点选/滚轮。
    input_.setMouseCaptured(ImGui::GetIO().WantCaptureMouse);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        ui::processImGuiEvent(ev);  // ImGui 输入透传
        input_.handleEvent(ev);     // 游戏输入翻译（空格/ESC双击/倍速/鼠标/缩放/F5/F12）
        if (ev.type == SDL_QUIT) quit_ = true;  // 退出走窗口关闭按钮（ESC 双击=暂停）
    }
}

void Application::applyInputs() {
    const app::InputActions& a = input_.actions();

    // ---- 相机（只改渲染视图，不碰模拟）----
    // 事件坐标已被 SDL 换算为逻辑坐标（窗口关联逻辑尺寸渲染器），pan 增量直接用。
    if (a.dragActive && (a.panDX != 0.0 || a.panDY != 0.0)) {
        camera_.pan(a.panDX, a.panDY);
    }
    if (a.wheelDelta != 0.0) {
        // 滚轮事件不携带位置：用 SDL_GetMouseState（窗口像素）+ 换算取当前光标逻辑位置作锚点。
        int wx = 0, wy = 0;
        SDL_GetMouseState(&wx, &wy);
        float mx = 0.0f, my = 0.0f;
        SDL_RenderWindowToLogical(ren_, wx, wy, &mx, &my);
        camera_.zoomAt(mx, my, std::pow(kZoomStep, a.wheelDelta));  // 锚定光标缩放
    }

    // ---- 速度 / 暂停（只改渲染节奏）----
    if (a.togglePause) paused_ = !paused_;
    const bool playerActive = (playerFactionId() > 0);
    if (!playerActive && a.speedSelect > 0) {
        // 玩家操控时 1-6 选兵种（数字键冲突）；非玩家 1-4 调速。
        speed_ = static_cast<double>(a.speedSelect);
    } else if (a.speedCycle != 0) {
        speed_ = cycleSpeed(speed_, a.speedCycle);  // +/- 恒可用
    }
    if (a.stepFrame) {
        // 步进：跑一个逻辑帧后进入暂停（再按再跑，测试用）。
        stepPending_ = true;
        paused_ = true;
    }

    // ---- 点选 / 截图 / 重载配置 ----
    if (a.click) pickSelection();
    if (a.screenshot) takeScreenshot();
    if (a.reloadConfig) reloadConfig();
    if (a.reloadPalette) reloadPalette();  // F7：双色调色板热重载（不重建模拟，⑫）

    // ---- 玩家意图（P2）：兵种键/城市点击/悬停城/校验（须在 pickSelection 之后）----
    updatePlayerIntent();
}

void Application::renderFrame() {
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);  // 背景黑（海 = 背景黑，见 §2.9）
    SDL_RenderClear(ren_);
    computeCounts();
    mapRenderer_->draw(sim_.map(), tileColors_, tileGradeColors_);
    // P13：城市渲染（基建格贴图 + 等级塔图标 + 高缩放细线围区）。画在陆军/子弹/特效之下，
    // 兵/子弹/特效压在城市上；渲染常数走 uiConfig_（视图层配置，F5 重载同步）。
    // P15：capitalStatus[c.id]（0 普通 / 1 正式首都 / 2 候补指定新都）→ 首都图标/更粗细线/虚化。
    if (cityRenderer_) {
        std::vector<int> capitalStatus(sim_.map().cities().size(), 0);
        for (const auto& f : sim_.factions()) {
            if (f.capitalState.capitalCityId >= 0
                && static_cast<size_t>(f.capitalState.capitalCityId) < capitalStatus.size())
                capitalStatus[static_cast<size_t>(f.capitalState.capitalCityId)] = 1;
            if (f.capitalState.designatedCityId >= 0
                && static_cast<size_t>(f.capitalState.designatedCityId) < capitalStatus.size()
                && capitalStatus[static_cast<size_t>(f.capitalState.designatedCityId)] != 1)
                capitalStatus[static_cast<size_t>(f.capitalState.designatedCityId)] = 2;
        }
        cityRenderer_->draw(sim_.map(), uiConfig_.render, capitalStatus);
    }
    armyRenderer_->draw(sim_);  // （2026-08 废除环绕：仅主位置单绘制）
    projectileRenderer_->draw(sim_);  // P9：子弹（兵上、特效下）
    effectRenderer_->draw(sim_);
    // 玩家产兵城/悬停指示圈（P2；画在特效之上、UI 之下）。
    // 2026-08-07：id → CityTarget{中心, w, h}，指示随城市基建地块尺寸缩放。
    if (cityMarker_) {
        const int playerFid = playerFactionId();
        const std::size_t nCity = sim_.map().cities().size();
        // 悬停/选中城 id 均为本帧 updatePlayerIntent 校验过的己方城市；防御性再查一次越界
        //（removeCity swap+pop 使 id 可能失效）。
        auto makeTarget = [&](int cid) -> std::optional<render::CityMarkerRenderer::CityTarget> {
            if (cid < 0 || static_cast<std::size_t>(cid) >= nCity) return std::nullopt;
            const auto& c = sim_.map().city(cid);
            return render::CityMarkerRenderer::CityTarget{c.centerX(), c.centerY(), c.w, c.h};
        };
        const auto hoverTgt = (hoverCityId_ >= 0) ? makeTarget(hoverCityId_) : std::nullopt;
        const auto selTgt = (playerFid > 0 && selCityId_ >= 0) ? makeTarget(selCityId_)
                                                               : std::nullopt;
        cityMarker_->draw(sim_, playerFid,
                          hoverTgt ? &*hoverTgt : nullptr, selTgt ? &*selTgt : nullptr);
    }
    ui::beginImGuiFrame();  // 坐标空间已由 imgui_impl_sdl2.cpp 补丁统一到逻辑尺寸
    drainEvents();  // P4：sim 事件 → 消息面板（每帧一次，渲染前）
    // UI 全局缩放（FontGlobalScale 缩放所有字体字形；面板宽度按 uiScale 调整，见各面板）。
    ImGui::GetIO().FontGlobalScale = uiScale_;
    // P8：玩家科研待选 → 强制暂停（科研弹窗内选择）。
    const int techPlayer = playerFactionId();
    if (techPlayer > 0 && sim_.faction(techPlayer).tech.researchPending) paused_ = true;
    ui::UiControls ctrl{speed_, paused_, uiScale_, capitalDesignationMode_};
    ui::UiRequests req;
    ui::PanelCtx pctx;
    pctx.sim = &sim_;
    pctx.selection = &selection_;
    pctx.seed = seed_;
    pctx.mapSeed = effectiveMapSeed();  // P6：主面板同时显示地图种子与主种子
    pctx.counts = &counts_;
    pctx.controls = &ctrl;
    pctx.requests = &req;
    pctx.sheet = &sheet_;
    pctx.manager = &panels_;
    panels_.draw(pctx);  // P3：主面板（固定左侧）+ 各可见可移动面板
    drawTechResearchModal(req);  // P8：玩家科研三选一（resarchPending 时绘制）
    ui::renderImGui(ren_);
    SDL_RenderPresent(ren_);

    // 面板控制回写（只改渲染节奏/UI）；请求在 Present 后处理（避免中途重建 sim_）。
    speed_ = ctrl.speed;
    paused_ = ctrl.paused;
    uiScale_ = ctrl.uiScale;
    if (req.stepFrame) {
        stepPending_ = true;  // 面板步进按钮（F6 等价）；下一循环迭代推进一帧
        paused_ = true;
    }
    if (req.reloadConfig) reloadConfig();
    if (req.endgameSummary) endgameSummary();
    if (req.toggleCapitalDesignation) capitalDesignationMode_ = !capitalDesignationMode_;  // P15
    if (req.techChosen) {  // P8：玩家科研选择 → 结算（-1=跳过）+ 恢复暂停
        if (sim_.choosePlayerTech(req.techIndex)) paused_ = false;
    }
    if (req.selectNone) {
        // 图标栏最左空框 → 不生产（P2 修正）。
        PlayerIntent intent = sim_.playerIntent();
        intent.selectedType = -1;
        sim_.setPlayerIntent(intent);
    } else if (req.selectUnitType >= 0) {
        // 兵种图标栏点击 → 写入玩家意图（P2）。
        PlayerIntent intent = sim_.playerIntent();
        intent.selectedType = req.selectUnitType;
        sim_.setPlayerIntent(intent);
    }
}

void Application::computeCounts() {
    counts_.armyPerFaction.fill(0);
    counts_.armyTotal = 0;
    const auto& reg = sim_.registry();
    for (auto e : reg.view<comp::UnitType, comp::FactionId>()) {
        if (reg.all_of<comp::Dead>(e)) continue;
        const int fid = reg.get<comp::FactionId>(e).value;
        if (fid >= 0 && fid < kFactionTotal) {
            ++counts_.armyPerFaction[static_cast<size_t>(fid)];
        }
        ++counts_.armyTotal;
    }
    counts_.effectCount = 0;
    for (auto e : reg.view<comp::EffectTypeId>()) {
        if (!reg.all_of<comp::Dead>(e)) ++counts_.effectCount;
    }
}

void Application::pickSelection() {
    const app::InputActions& a = input_.actions();
    // 事件坐标已由 SDL 换算为逻辑坐标（窗口关联逻辑尺寸渲染器），直接使用。
    const double clickX = a.mouseX;
    const double clickY = a.mouseY;

    // 1) 兵：屏幕空间点选，与渲染视觉对齐。
    //    - 锚点 = 兵 sprite 的【绘制中心】= (toScreenXi(x), toScreenYi(y))。
    //      （渲染曾加 spriteSize/2 的 y 偏移，后取消；点选与渲染必须一致，故也取消。）
    //    - 半径 = 该兵种 visualRadius * zoom（config.units 表，由 army.png 子图量得；
    //      不同兵种视觉大小不同，统一半格阈值会点到邻近普通兵的空白区）。
    //    - 判定取 ratio = 距离/半径 最小者（相对其视觉最居中），且 < 1 才算命中。
    //（2026-08 废除环绕：仅主位置点选，不再对贴界副本取最小距离。）
    const double z = camera_.zoom();
    const auto& ucfg = sim_.config().units;
    entt::entity best = entt::null;
    double bestRatio = 1.0;  // ratio<1 才命中
    const auto& reg = sim_.registry();
    for (auto e : reg.view<comp::Position, comp::FactionId, comp::UnitType>()) {
        if (reg.all_of<comp::Dead>(e)) continue;
        // 子弹（comp::Projectile）不参与兵点选（与渲染排除/计数一致；近战不打子弹）。
        if (reg.all_of<comp::Projectile>(e)) continue;
        const auto& p = reg.get<comp::Position>(e);
        const int t = static_cast<int>(reg.get<comp::UnitType>(e).type);
        const double r = ucfg[static_cast<size_t>(t)].visualRadius * z;
        if (r <= 0.0) continue;
        const int sx = camera_.toScreenXi(p.x);
        const int sy = camera_.toScreenYi(p.y);
        const double ratio = std::hypot(clickX - sx, clickY - sy) / r;
        if (ratio < bestRatio) {
            bestRatio = ratio;
            best = e;
        }
    }
    if (best != entt::null) {
        selection_.kind = app::Selection::Kind::Army;
        selection_.army = best;
        return;
    }

    // 2) 城市/陆地格（点选到海或界外 → 清空）。
    const double wx = camera_.toWorldX(clickX);
    const double wy = camera_.toWorldY(clickY);
    // P12：密铺取格（方 = floor；六/三 = worldToCell）。
    const int cellIdx = sim_.map().geom().worldToCell(wx, wy);
    if (cellIdx >= 0) {
        const MapCell& cell = sim_.map().atIndex(cellIdx);
        if (cell.land) {
            // 格坐标（列, 行）供显示。
            int gx = cellIdx % sim_.map().geom().cols;
            int gy = cellIdx / sim_.map().geom().cols;
            if (sim_.map().tiling() != TilingType::Square) {
                int rr, cc, bb;
                sim_.map().geom().indexToRowCol(cellIdx, rr, cc, bb);
                gx = cc;
                gy = rr;
            }
            selection_.kind = app::Selection::Kind::Cell;
            selection_.cellIndex = cellIdx;
            selection_.cellX = gx;
            selection_.cellY = gy;
            return;
        }
    }
    selection_.kind = app::Selection::Kind::None;
}

int Application::playerFactionId() const {
    // 以运行时 Faction.aiId 为准（读档后也正确；options 不随快照）。
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
        if (sim_.faction(id).aiId == 1) return id;
    }
    return 0;
}

void Application::initPlayerIntent() {
    const int playerFid = playerFactionId();
    if (playerFid <= 0) return;  // 非玩家模式不初始化
    PlayerIntent intent;
    intent.active = true;
    intent.factionId = playerFid;
    intent.selectedType = static_cast<int>(ArmyType::normal);  // 默认普通兵
    selCityId_ = -1;
    // P13：cityIds[j] → 注册表城市；意图坐标 = 城市【几何中心】（2026-08-07 用户要求选取落在
    // 产兵城中央——产兵在中心，选取也以中心比对）。
    const auto& cityIds = sim_.faction(playerFid).cityIds;
    if (!cityIds.empty()) {
        // 初始产兵城 = 初始城市（首都，conquer 最先插入）。
        const auto& city = sim_.map().city(static_cast<size_t>(cityIds[0]));
        selCityId_ = cityIds[0];
        intent.cityX = city.centerX();
        intent.cityY = city.centerY();
    }
    sim_.setPlayerIntent(intent);
    paused_ = true;  // 有玩家 → 进入游戏即暂停（P2 修正）
    spdlog::info("player faction {}: initial intent normal @ center ({:.1f},{:.1f}) — paused",
                 playerFid, intent.cityX, intent.cityY);
}

void Application::updatePlayerIntent() {
    const int playerFid = playerFactionId();
    if (playerFid <= 0) {
        hoverCityId_ = selCityId_ = -1;
        return;
    }
    const app::InputActions& a = input_.actions();
    PlayerIntent intent = sim_.playerIntent();
    intent.active = true;
    intent.factionId = playerFid;

    // 兵种键（数字键 1-6）与不产键（`，2026-08-03）。
    if (a.selectNone) {
        intent.selectedType = -1;
    } else if (a.selectUnitType >= 0 && a.selectUnitType < kArmyTypeCount) {
        intent.selectedType = a.selectUnitType;
    }
    // 校验兵种（含"不产" = -1）。
    if (intent.selectedType < 0 || intent.selectedType >= kArmyTypeCount) intent.selectedType = -1;
    // 校验产兵城：已易主/消失 → 清空（removeCity 会 swap+pop，下标可能失效）。
    // P13：意图坐标 = 城市几何中心；比对 cityIds[j] → 注册表城市 centerX/centerY（中心匹配）。
    if (intent.cityX >= 0.0) {
        bool owned = false;
        for (int cid : sim_.faction(playerFid).cityIds) {
            const auto& city = sim_.map().city(static_cast<size_t>(cid));
            if (std::fabs(city.centerX() - intent.cityX) < 1e-6 &&
                std::fabs(city.centerY() - intent.cityY) < 1e-6) {
                owned = true;
                selCityId_ = cid;
                break;
            }
        }
        if (!owned) {
            intent.cityX = intent.cityY = -1.0;
            selCityId_ = -1;
        }
    } else if (selCityId_ >= 0) {
        selCityId_ = -1;
    }

    // 城市命中（屏幕空间判定，参考 pickSelection 的兵点选）：鼠标到己方城市中心的
    // 屏幕距离 ≤ 阈值 → 命中最近者。阈值随缩放（= hoverCityRadius 格 × cellPx），
    // 下限 20px 防缩小时过小。悬停圈与点击共用同一判定（所见即所选）。
    const double cellPx = camera_.cellPx();
    const double selR = std::max(20.0, sim_.config().player.hoverCityRadius * cellPx);
    int hitCityId = -1;
    double hitDist = std::numeric_limits<double>::max();
    // P13：多格城市以城市中心（baseX+w/2, baseY+h/2）为命中/指示锚点（2026-08-07 起意图坐标
    // 即中心，故命中判定与产兵坐标天然一致）。
    for (int cid : sim_.faction(playerFid).cityIds) {
        const auto& city = sim_.map().city(static_cast<size_t>(cid));
        // 2026-08 收敛：连续变换（toScreenX/Y 双精度）算距离——城市中心可能是 .5 等
        // 非整数坐标，int 版会抹掉小数（旧混用；阈值 20px 量级下差别极小，但语义应统一）。
        const double d = std::hypot(a.mouseX - camera_.toScreenX(city.centerX()),
                                    a.mouseY - camera_.toScreenY(city.centerY()));
        if (d <= selR && d < hitDist) {
            hitDist = d;
            hitCityId = cid;
        }
    }
    hoverCityId_ = hitCityId;

    // 城市点击：P15"指定首都"模式（且玩家无正式首都）→ 指定候补新都（立即迁都态下 CapitalSystem
    // 下一 tick 迁都）；否则选为产兵城（意图存中心坐标）。
    if (a.click && hitCityId >= 0) {
        if (capitalDesignationMode_ && sim_.faction(playerFid).capitalState.capitalCityId < 0) {
            // 交互（用户建议）：指定成功（点击可行的己方城）→ 退出"指定首都"状态。
            if (sim_.setDesignatedCapital(playerFid, hitCityId)) capitalDesignationMode_ = false;
        } else {
            const auto& city = sim_.map().city(static_cast<size_t>(hitCityId));
            intent.cityX = city.centerX();
            intent.cityY = city.centerY();
            selCityId_ = hitCityId;
        }
    }

    sim_.setPlayerIntent(intent);
}

bool Application::applyPaletteColors() {
    // 双色势力渲染统一重烘焙（init / F5 / F7 共用；视觉工程改进 ⑫）。
    // 数据源 = uiConfig_（data/ 下合并后的配置）：factions[i].color=主色、.secondary=副色；
    // render.tileMix / render.city.mix = 地块格/城市图标混合比例。
    // 势力 1..8 主副色对 → 兵表双色烘焙（TwoToneTintCache：反解模板 → 按 (主,副) 合成）。
    std::vector<std::pair<std::array<int, 3>, std::array<int, 3>>> pairs;
    pairs.reserve(kPlayerFactionCount);
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
        const auto& f = uiConfig_.factions[static_cast<size_t>(id)];
        pairs.emplace_back(f.color, f.secondary);
    }
    if (!sheet_.load(ren_, pairs)) {
        spdlog::error("applyPaletteColors: sprite sheet load failed");
        return false;
    }
    // 主色表（含中立 0 共 9 项）：山贴图 / 城细线与图标共用。
    std::vector<std::array<int, 3>> primaries;
    primaries.reserve(kFactionTotal);
    for (int id = 0; id < kFactionTotal; ++id)
        primaries.push_back(uiConfig_.factions[static_cast<size_t>(id)].color);
    if (mapRenderer_) mapRenderer_->reloadColors(primaries);
    if (cityRenderer_) cityRenderer_->reloadColors(uiConfig_, uiConfig_.render.city.iconDarken);
    // 玩家指示（环/箭头）与特效爆炸圆：势力 1..8 主色。
    std::vector<std::array<int, 3>> primaries8;
    primaries8.reserve(kPlayerFactionCount);
    for (int id = 1; id <= kPlayerFactionCount; ++id)
        primaries8.push_back(uiConfig_.factions[static_cast<size_t>(id)].color);
    if (cityMarker_) cityMarker_->reloadColors(primaries8);
    if (effectRenderer_) effectRenderer_->reloadColors(primaries8);
    // 地块填色缓存（MapRenderer::draw 用；主:副:白 加权平均）。
    rebuildTilePalette();
    return true;
}

void Application::rebuildTilePalette() {
    // 地块填色：tileColors_ = 渐变中点（t=0.5；方/六/三与单色用地）；
    // tileGradeColors_ = 双色密铺分档配色（arch/laves：下标=档位，按当前密铺的分档数取
    // 不同 t：t = 档位/(档数-1)）。档数与地图无关（只由密铺类型决定）→ 用 1×1 几何推算；
    // 未就绪时用 uiConfig_ 密铺（frame 阶段的 sim 已构建 → simReady_ 优先）。
    const TilingType tt = simReady_ ? sim_.map().tiling() : uiConfig_.map.tilingType();
    for (int id = 0; id < kFactionTotal; ++id)
        tileColors_[static_cast<size_t>(id)] = uiConfig_.factionTileColor(id);
    const TilingGeom tg{tt, 1, 1};
    const int paletteSize = std::max(0, tg.tilePaletteSize());
    tileGradeColors_.clear();
    tileGradeColors_.reserve(static_cast<size_t>(paletteSize));
    for (int j = 0; j < paletteSize; ++j) {
        const double t = static_cast<double>(j) / static_cast<double>(paletteSize - 1);
        std::array<std::array<int, 3>, kFactionTotal> row;
        for (int id = 0; id < kFactionTotal; ++id)
            row[static_cast<size_t>(id)] = uiConfig_.factionTileColor(id, t);
        tileGradeColors_.push_back(row);
    }
}

void Application::reloadPalette() {
    // F7：按当前 uiConfig_（data/ 下配置的 factions 主副色 / render.tile / render.city.mix）
    // 重烘焙全部势力色（不重建模拟，当前局面保留）。改 config.json 后按 F5 会连带重建。
    applyPaletteColors();
    spdlog::info("palette re-baked from config (sim untouched)");
}

void Application::reloadConfig() {
    // 调试功能：读盘重建模拟（同种子重启），当前局面丢弃。破坏性，仅调试用。
    // 沿用当前地图选择（--map 覆盖 / 菜单 options / 随机图 P6），避免 F5 后地图被重置回 config 默认（P5 改版修复）。
    Config cfg = Config::loadFromFile(configPath_.empty() ? kDefaultConfigPath : configPath_);
    resolveMapSelection(cfg);
    Simulation fresh(cfg, seed_, effectiveMapSeed());
    // 用户反馈：重载后玩家无法游玩（变回全默认 AI）——fresh 必须沿用当前 options
    //（含玩家 aiId=1、势力出场、地图选择），不能走默认 init()（全 aiId=0）。
    if (!fresh.init(sim_.options())) {
        spdlog::error("config reload: re-init from '{}' failed, keeping current sim",
                      configPath_);
        return;
    }
    sim_ = std::move(fresh);
    uiConfig_ = sim_.config();  // 随机图尺寸可能变 → 重配相机（F5 后平移钳制用新尺寸）
    syncCameraToMap();
    selection_ = app::Selection{};
    paused_ = false;
    if (messagePanel_) messagePanel_->clear();  // P4：清掉旧局消息
    // 势力色可能被 config.json 改动 → 统一重烘焙全部势力色（双色系统，视觉工程改进 ⑫：
    // factions[i].color/secondary + render.tile / render.city.mix 为渲染色唯一数据源）。
    applyPaletteColors();
    initPlayerIntent();  // 有玩家：重建后重置意图 + 暂停
    capitalDesignationMode_ = false;  // P15：重建后退出"指定首都"模式
    spdlog::info("config reloaded '{}' → sim restarted (seed={}, tick=0)", configPath_, seed_);
}

void Application::endgameSummary() {
    // 与 headless --summary 同款终局摘要（写日志；不改模拟状态）。
    spdlog::info("=== endgame summary (tick {}) ===", sim_.tickCount());
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
        const auto& f = sim_.faction(id);
        spdlog::info("  faction {}: land={} city={} army={} 留存经济={:.2f}", id, f.landCount,
                     f.cityCount, counts_.armyPerFaction[static_cast<size_t>(id)], f.economy);
    }
}

double Application::cycleSpeed(double cur, int dir) {
    // 档位含 <1x（0.25/0.5，隔帧执行逻辑帧，测试用）；+/- 循环步进。
    static constexpr std::array<double, 6> kSteps = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    int idx = 0;
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < kSteps.size(); ++i) {
        const double d = std::fabs(cur - kSteps[i]);
        if (d < best) {
            best = d;
            idx = static_cast<int>(i);
        }
    }
    idx = std::clamp(idx + dir, 0, static_cast<int>(kSteps.size()) - 1);
    return kSteps[static_cast<size_t>(idx)];
}

void Application::setupPanels() {
    // P3 面板框架：主面板固定左侧；消息面板（P4）默认隐藏，主面板开关打开。
    panels_.registerPanel(std::make_unique<ui::DebugPanel>());    // 主面板（mainPanel=true）
    auto msg = std::make_unique<ui::MessagePanel>();
    messagePanel_ = msg.get();  // P4：drain 事件需要直接访问（所有权归 PanelManager）
    panels_.registerPanel(std::move(msg));
    panels_.registerPanel(std::make_unique<ui::StatsPanel>());  // P11：统计面板（默认隐藏）
    panels_.registerPanel(std::make_unique<ui::TechPanel>());   // P8：科技面板（默认隐藏）
    // 布局恢复：按 id 匹配回填位置/大小/可见性（主面板恒显示，忽略）。
    panels_.applyLayout(options_.panels);
    spdlog::info("panel system ready ({} panel(s) registered)", panels_.panels().size());
}

void Application::drainEvents() {
    // P4：每帧取走 sim 产生的事件 → 消息面板（纯展示通道，不碰模拟状态）。
    if (messagePanel_) messagePanel_->addEvents(sim_.takeEvents(), sim_);
}

void Application::drawTechResearchModal(ui::UiRequests& req) {
    const int playerFid = playerFactionId();
    if (playerFid <= 0) return;
    const Faction& f = sim_.faction(playerFid);
    if (!f.tech.researchPending) return;  // 无待选不弹
    const auto& defs = sim_.config().tech.techs;
    if (defs.empty()) return;

    // 思路"科技细节"：升级项旧数值黄、新数值绿。
    const ImVec4 kNewGreen(0.35f, 0.95f, 0.35f, 1.0f);
    const ImVec4 kOldYellow(0.95f, 0.9f, 0.2f, 1.0f);

    ImGui::SetNextWindowSize(ImVec2(ui::scaled(560.0f, uiScale_), 0.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::Begin("科研选择", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                     | ImGuiWindowFlags_NoResize);

    const auto& fc = f.color;
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(fc[0] / 255.0f, fc[1] / 255.0f, fc[2] / 255.0f, 1.0f));
    ImGui::Text("势力 %s 获得科研机会", factionShortName(playerFid));
    ImGui::PopStyleColor();
    ImGui::TextUnformatted("科技点已攒满阈值，选择一个科技（升级项旧值黄、新值绿）：");
    ImGui::Separator();

    int choice = -2;  // -2 未选；-1 跳过；>=0 科技下标
    for (int c : f.tech.candidates) {
        if (c < 0 || c >= static_cast<int>(defs.size())) continue;
        const auto& def = defs[static_cast<size_t>(c)];
        const int cur = (c < static_cast<int>(f.tech.levels.size())) ? f.tech.levels[c] : 0;
        const int newLevel = cur + 1;
        if (newLevel <= 0 || newLevel > static_cast<int>(def.levels.size())) continue;
        const auto& newLv = def.levels[static_cast<size_t>(newLevel - 1)];
        const std::string newVal = buffValueText(newLv.type, newLv.magnitude);
        const bool upgrade = cur >= 1;  // 已拥有 → 升级项（显示旧值）
        std::string oldVal;
        if (upgrade) {
            const auto& oldLv = def.levels[static_cast<size_t>(cur - 1)];
            oldVal = buffValueText(oldLv.type, oldLv.magnitude);
        }

        ImGui::PushID(c);
        if (ImGui::Selectable(def.name.c_str())) choice = c;
        // 效果行：desc + 新值（绿）+（旧 值黄）。
        ImGui::TextUnformatted("      ");
        ImGui::SameLine();
        ImGui::TextUnformatted(def.desc.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kNewGreen);
        ImGui::TextUnformatted(newVal.c_str());
        ImGui::PopStyleColor();
        if (upgrade) {
            ImGui::SameLine();
            ImGui::TextUnformatted("（");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kOldYellow);
            ImGui::TextUnformatted(oldVal.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted("）");
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    // 2026-08 细节改进：跳过不提升阈值（下次升级所需科技点数不变）。
    if (ImGui::Button("跳过（放弃本次科研机会 · 下次阈值不变）")) choice = -1;

    if (choice != -2) {  // 用户已做选择 → 写请求（Application renderFrame 消费）
        req.techChosen = true;
        req.techIndex = choice;
    }
    ImGui::End();
}

void Application::syncPanelLayout() {
    // 把当前面板布局写回 options_.panels → 退出时随 options.json 持久化（重启读回）。
    options_.panels = panels_.layout();
}

int Application::run() {
    if (!init()) return 1;

    // P1 菜单阶段：skipMenu=false 且未注入预建模拟 → 先跑菜单；点「开始游戏」才进游戏。
    if (!prebuilt_ && !skipMenu_) {
        if (!runMenu()) {
            spdlog::info("menu closed, bye");
            return 0;  // 退出菜单
        }
    }
    if (!ensureSimulation()) return 1;
    initPlayerIntent();  // 有玩家：初始意图（普通兵+首都）+ 进入即暂停（P2 修正）

    // 固定步长主循环（§3.7）：模拟每 1/60s 一步，渲染独立帧率。
    // speed_/paused_ 只改变 tick 节奏，不改变模拟状态本身（确定性成立）。
    // 2026-08 工程改进：累加器抽为 FixedTimestep（纯逻辑，可单测；语义与旧内联实现一致）。
    const double step = 1.0 / sim_.config().sim.tickRate;
    lw::FixedTimestep timestep(step);
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();

    while (!quit_) {
        input_.beginFrame();
        processEvents();
        applyInputs();

        const Uint64 now = SDL_GetPerformanceCounter();
        const double frame = static_cast<double>(now - last) / perfFreq;
        last = now;
        if (stepPending_) {
            // 步进：无论暂停/倍速，本帧恰好推进一个逻辑帧，然后进入暂停。
            sim_.tick();
            stepPending_ = false;
            paused_ = true;
            timestep.reset();
        } else if (!paused_) {
            // 固定步长累加：speed_ 支持 <1x（0.25x/0.5x）——累积得慢，逻辑帧自然隔帧执行。
            timestep.advance(frame * speed_);
            for (int n = timestep.consumeTicks(); n > 0; --n) sim_.tick();
        } else {
            timestep.reset();
        }

        renderFrame();

        // QA 钩子：到达目标 tick 后截图并退出（须在 renderFrame 后，捕获已呈现帧）。
        if (screenshotTick_ >= 0 && !screenshotTaken_ &&
            sim_.tickCount() >= static_cast<std::uint64_t>(screenshotTick_)) {
            screenshotTaken_ = true;
            saveScreenshot(screenshotPath_);
            quit_ = true;
        }

        // vsync 生效时 Present 会阻塞到垂直回扫（自限帧率），再加 Delay 会白拖慢 ~1ms
        // （→ ~56 t/s 慢动作）；仅无 vsync（软件兜底等）时用 Delay 让出 CPU。
        if (!vsync_) SDL_Delay(1);
    }

    // P3 面板布局持久化：退出时把当前面板位置/可见性写回 options.json（重启读回）。
    syncPanelLayout();
    if (!optionsPath_.empty()) options_.saveToFile(optionsPath_);

    spdlog::info("bye ({} ticks)", sim_.tickCount());
    return 0;
}

bool Application::runMenu() {
    mapFiles_ = ui::enumerateMapFiles("data");
    spdlog::info("menu: {} map file(s) found", mapFiles_.size());
    // P6：菜单跨帧状态（二级选图屏/草稿/预览缓存）。局部持有 → 返回前析构（释放预览纹理，
    // ren_ 尚存活）。预览纹理随菜单结束清理，游戏阶段不占内存。
    ui::MenuState menuState;
    menuState.mainSeed = seed_;
    while (!quit_ && !menuStarted_) {
        input_.beginFrame();
        processEvents();

        // 清黑背景，只画菜单（游戏渲染器组尚未使用）。
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
        SDL_RenderClear(ren_);
        ui::beginImGuiFrame();
        ImGui::GetIO().FontGlobalScale = uiScale_;
        // uiScale 传引用：菜单内滑条可调整，调整后带入游戏面板（DebugPanel 共用）。
        const ui::MenuResult res = ui::drawMenu(menuState, ren_, options_, uiConfig_, mapFiles_,
                                                uiScale_);
        ui::renderImGui(ren_);
        SDL_RenderPresent(ren_);

        if (res.started) {
            menuStarted_ = true;
            options_.saveToFile(optionsPath_);  // 思路 1：开始游戏时保存 options.json
        }
        if (res.quit) quit_ = true;
        if (!vsync_) SDL_Delay(16);
    }
    return menuStarted_;
}

bool Application::ensureSimulation() {
    if (simReady_) return true;  // 已注入（--load）
    return buildSimulation();
}

void Application::resolveMapSelection(Config& cfg) {
    // 当前地图选择：--map 覆盖 > 菜单 options 选择 > 随机图生成（P6）> config 默认。
    if (!mapOverride_.empty()) {
        cfg.map.file = mapOverride_;
        return;
    }
    if (options_.map.kind == MapSelection::Kind::File && !options_.map.file.empty()) {
        cfg.map.file = options_.map.file;
        return;
    }
    if (options_.map.kind == MapSelection::Kind::Random) {
        // P6：生成地形基图 → 走现有加载路径（单一加载路径；P12：六/三 = lwmap）。
        // 确定性：(seed, 参数 + 密铺)。
        const MapGenParams p{options_.map.width,   options_.map.height,
                             options_.map.seaRatio, options_.map.mountainDensity,
                             options_.map.cityDensity, cfg.map.cityMountainWeight,
                             options_.map.forceCoast, tilingFromName(options_.map.tiling)};
        const std::string path = MapGenerator::defaultPath(options_.map.randomSeed, p);
        if (MapGenerator::generate(path, options_.map.randomSeed, p)) {
            cfg.map.width = p.width;  // 只在生成成功后覆盖尺寸/文件（失败保底可启动）
            cfg.map.height = p.height;
            cfg.map.tiling = options_.map.tiling;  // P12：密铺
            cfg.map.file = path;
            return;
        }
        spdlog::error("random map generate failed, falling back to '{}'", cfg.map.file);
        return;
    }
    // 回退 config 默认。
}

bool Application::buildSimulation() {
    Config cfg = Config::loadFromFile(configPath_.empty() ? kDefaultConfigPath : configPath_);
    resolveMapSelection(cfg);
    // P6 RNG 分离：主种子 seed_ 驱动首都/后续；地图种子 effectiveMapSeed() 驱动地图骰子。
    Simulation fresh(cfg, seed_, effectiveMapSeed());
    if (!fresh.init(options_)) {
        spdlog::error("simulation init failed (map missing?): '{}'", cfg.map.file);
        return false;
    }
    sim_ = std::move(fresh);
    simReady_ = true;
    uiConfig_ = sim_.config();
    rebuildTilePalette();  // 双色密铺分档：按当前地图密铺重算地块配色（tileColorIndex 用）
    syncCameraToMap();  // 随机图尺寸可能 ≠ 默认 → 重配屏幕变换与相机（大图平移够到边缘）
    spdlog::info("simulation ready (seed={}, mapSeed={}, map='{}', tick=0)", seed_,
                 sim_.mapSeed(), cfg.map.file);
    return true;
}

void Application::syncCameraToMap() {
    // 随机图长宽可与 config 默认（105×95）不同 → 用当前 sim 的 config 重配 tf_ 与 camera_
    // （tf_.mapHeight、camera mapW/mapH 参与屏幕变换与平移钳制，旧值会让大图边缘够不到）。
    const auto& mcfg = uiConfig_.map;
    const auto& rcfg = uiConfig_.render;
    // P12：相机按当前地图世界范围配置（随机图尺寸可能 ≠ 默认 → 重配屏幕变换与相机）。
    const TilingGeom tg = sim_.map().geom();
    tf_ = math::ScreenTransform{static_cast<double>(mcfg.blockSize), mcfg.panelWidth,
                                tg.worldHeight()};
    camera_.configure(tf_, rcfg.windowWidth, rcfg.windowHeight, tg.worldWidth(), tg.worldHeight());
}

void Application::takeScreenshot() {
    // 2026-08 工程改进：截图入 userdata/screenshots/（init 已确保目录存在）。
    saveScreenshot(std::string(kScreenshotDir) + "/screenshot_" + std::to_string(screenshotCounter_++) +
                   ".png");
}

void Application::saveScreenshot(const std::string& name) {
    // 渲染目标为逻辑尺寸（SDL_RenderSetLogicalSize = config 窗口尺寸），
    // 窗口缩放后 GetWindowSize 返回的是缩放后尺寸 → 必须用逻辑尺寸读取，
    // 否则 RenderReadPixels 与目标不匹配会截断（窗口适配后的回归）。
    const int w = sim_.config().render.windowWidth;
    const int h = sim_.config().render.windowHeight;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        spdlog::error("screenshot: SDL_CreateRGBSurfaceWithFormat failed: {}", SDL_GetError());
        return;
    }
    if (SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_RGBA32, surf->pixels, surf->pitch) !=
        0) {
        spdlog::error("screenshot: SDL_RenderReadPixels failed: {}", SDL_GetError());
        SDL_FreeSurface(surf);
        return;
    }
    if (IMG_SavePNG(surf, name.c_str()) != 0) {
        spdlog::error("screenshot: IMG_SavePNG '{}' failed: {}", name, IMG_GetError());
    } else {
        spdlog::info("screenshot saved: {}", name);
    }
    SDL_FreeSurface(surf);
}

}  // namespace lw
