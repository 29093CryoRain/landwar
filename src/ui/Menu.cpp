// Menu.cpp — 主菜单 + 二级「选择地图」界面（开发计划 P1/P6）。
#include "ui/Menu.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <numeric>

#ifdef _WIN32
#include <windows.h>  // FindFirstFileA（枚举地图文件；避免 std::filesystem——MSYS2 运行时
                      // libstdc++-6.dll 不导出其符号，会导致 STATUS_ENTRYPOINT_NOT_FOUND）
#else
#include <filesystem>
#endif

#include "core/GameDefs.h"
#include "core/Random.h"
#include "render/MapPreview.h"
#include "ui/UiScale.h"
#include "ui/UiText.h"
#include "world/MapGenerator.h"
#include "world/tiling/Tiling.h"

namespace lw::ui {

namespace {

// 菜单基准宽度（uiScale=1 时）；窗口/控件显式宽度都按 uiScale 缩放（FontGlobalScale 只放大
// 字形，像素宽不会跟着变——见 DebugPanel 同款处理）。
constexpr float kBaseMenuWidth = 620.0f;
// 选图页更宽（预装缩略图 + 参数区并排）。
constexpr float kBaseMapSelectWidth = 800.0f;
// 预览缩略图目标宽度（像素；实际纹理 = cell×宽 × cell×高）。
constexpr int kPreviewW = 180;

// 文件名（去掉 data/ 前缀），用于预装图列表显示。
const char* baseName(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path.c_str() : path.c_str() + pos + 1;
}

// ---- 密度滑条的平方映射（2026-08-06 用户要求：平方形式替代指数/对数）----
// 滑条位置 t∈[0,1] ↔ 密度∈[lo,hi]：density = lo + (hi-lo)·t²。t 小处导数小 → 低端可细调；
// t=1 → hi（上限不变）。山 lo=0（最小值 0）、城 lo=0.0005（最小值不变）。
// 滑条位置存在 MenuState（mtnT/cityT）跨帧稳定——ImGui 直改它，不每帧从密度重算（手感修正）。
constexpr float kMtnLo = 0.0f, kMtnHi = 0.60f;
constexpr float kCityLo = 0.0005f, kCityHi = 0.30f;
float squareTToDensity(float t, float lo, float hi) {
    return lo + (hi - lo) * t * t;
}
float squareDensityToT(float d, float lo, float hi) {
    if (d <= lo) return 0.0f;
    if (d >= hi) return 1.0f;
    return std::sqrt((d - lo) / (hi - lo));
}

// 把菜单草稿提交回 options（「返回」时调用）。
void commitDrafts(MenuState& st, Options& options) {
    options.map.randomSeed = st.draftSeed;
    if (st.randomMode) {
        options.map.kind = MapSelection::Kind::Random;
        options.map.width = st.randW;
        options.map.height = st.randH;
        options.map.seaRatio = st.seaRatio;
        options.map.mountainDensity = st.mtnDensity;
        options.map.cityDensity = st.cityDensity;
        options.map.forceCoast = st.forceCoast;
    } else {
        options.map.kind = MapSelection::Kind::File;
        if (!st.draftFile.empty()) options.map.file = st.draftFile;
    }
    // P12：密铺（随机图生成用；预装 BMP 恒方形）。
    options.map.tiling = tilingName(st.tiling);
}

// 首次进入选图页：从 options 初始化草稿；地图种子未设置（0）→ 自动随机（用户定夺 2026-08）。
void initDrafts(MenuState& st, const Options& options) {
    st.draftSeed = options.map.randomSeed;
    if (st.draftSeed == 0) st.draftSeed = Rng::randomSeed();
    st.randomMode = (options.map.kind == MapSelection::Kind::Random);
    st.draftFile = options.map.file;
    st.randW = options.map.width;
    st.randH = options.map.height;
    st.seaRatio = static_cast<float>(options.map.seaRatio);
    st.mtnDensity = static_cast<float>(options.map.mountainDensity);
    st.cityDensity = static_cast<float>(options.map.cityDensity);
    st.mtnT = squareDensityToT(st.mtnDensity, kMtnLo, kMtnHi);   // 密度 → 滑条位置（平方域）
    st.cityT = squareDensityToT(st.cityDensity, kCityLo, kCityHi);
    st.forceCoast = options.map.forceCoast;
    st.tiling = tilingFromName(options.map.tiling);  // P12：密铺（默认 square）
    st.seededOnce = true;
    st.previews.clear();
}

// 加载预装地图预览（缓存 key = file + seed；未缓存才读盘 + 掷骰 + 渲染）。失败返回 nullptr。
SDL_Texture* filePreview(MenuState& st, SDL_Renderer* ren, const Config& cfg,
                         const std::string& file, int* outW, int* outH) {
    const std::string key = file + "#" + std::to_string(st.draftSeed);
    if (SDL_Texture* cached = st.previews.lookup(key, outW, outH)) return cached;
    Config c = cfg;
    c.map.file = file;
    Map map;
    map.configure(c.map);
    map.setTerrain(c.terrain);
    Rng r(st.draftSeed);  // 山/城骰子用当前地图种子（预览即所见）
    if (!map.loadFromBmp(file, r)) {
        spdlog::warn("menu preview: load '{}' failed", file);
        return nullptr;
    }
    return st.previews.get(ren, key, map, kPreviewW, outW, outH);
}

// 生成随机图（确定性：seed+params；方 = BMP，六/三 = lwmap）；成功返回 true。
bool generateRandomMap(MenuState& st, const Config& cfg, SDL_Renderer* ren) {
    const MapGenParams p{st.randW, st.randH, st.seaRatio, st.mtnDensity, st.cityDensity,
                         cfg.map.cityMountainWeight, st.forceCoast, st.tiling,
                         cfg.map.forceCoastRangeMultiplier, cfg.map.forceCoastStrengthMultiplier};
    st.genPath = MapGenerator::defaultPath(st.draftSeed, p);
    st.genError.clear();
    if (!MapGenerator::generate(st.genPath, st.draftSeed, p)) {
        st.genError = "随机图生成失败（写盘错误）";
        return false;
    }
    Config c = cfg;
    c.map.width = st.randW;
    c.map.height = st.randH;
    c.map.tiling = tilingName(st.tiling);  // P12：密铺（load 分发 + 预览几何）
    c.map.file = st.genPath;
    Map map;
    map.configure(c.map);
    map.setTerrain(c.terrain);
    Rng r(st.draftSeed);
    const bool ok = (st.tiling == TilingType::Square)
                        ? map.loadFromBmp(st.genPath, r)
                        : map.loadFromLwmap(st.genPath, r);
    if (!ok) {
        st.genError = "随机图加载失败";
        return false;
    }
    int tw = 0, th = 0;
    if (!st.previews.get(ren, st.genPath, map, kPreviewW, &tw, &th)) {
        st.genError = "随机图预览渲染失败";
        return false;
    }
    return true;
}

// 主屏：地图摘要 + 选择地图入口 + 势力配置 + 开始/退出。
// mapFiles 仅在二级选图屏（drawMapSelectScreen）使用，主屏不需（保留签名对称）。
void drawMainScreen(MenuState& st, Options& options, const Config& cfg,
                    [[maybe_unused]] const std::vector<std::string>& mapFiles, float& uiScale,
                    MenuResult& res) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::max(420.0f, kBaseMenuWidth * uiScale), 0.0f),
                             ImGuiCond_Always);
    if (!ImGui::Begin("###menu", nullptr,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("领土战争");
    ImGui::Separator();

    // ---- 地图选择（P6：二级选图界面入口）----
    ImGui::TextUnformatted("地图");
    char summary[96];
    if (options.map.kind == MapSelection::Kind::Random) {
        std::snprintf(summary, sizeof(summary), "随机地图 (seed: %u) %dx%d", options.map.randomSeed,
                      options.map.width, options.map.height);
    } else {
        std::snprintf(summary, sizeof(summary), "%s", baseName(options.map.file));
    }
    ImGui::TextUnformatted(summary);
    ImGui::SameLine();
    if (ImGui::Button("选择地图…", ImVec2(scaled(kButtonWidth, uiScale), 0.0f))) {
        st.screen = MenuState::Screen::MapSelect;
        if (!st.seededOnce) initDrafts(st, options);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("势力配置");

    // ---- 每个势力：出场勾选 + AI 下拉 ----
    char buf[48];
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        auto& slot = options.factions[static_cast<size_t>(i)];
        const std::array<int, 3>& col =
            (i + 1 < static_cast<int>(cfg.factions.size()))
                ? cfg.factions[static_cast<size_t>(i + 1)].color
                : std::array<int, 3>{127, 127, 127};
        ImGui::PushStyleColor(ImGuiCol_Text, rgbToImVec4(col));
        ImGui::TextUnformatted(factionShortName(i + 1));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        std::snprintf(buf, sizeof(buf), "出场##f%d", i + 1);
        ImGui::Checkbox(buf, &slot.enabled);
        ImGui::SameLine();
        std::snprintf(buf, sizeof(buf), "##ai%d", i + 1);
        ImGui::SetNextItemWidth(scaled(kDropdownWidth, uiScale));
        int ai = slot.aiId;
        const char* aiItems[] = {"默认AI", "玩家"};
        if (ImGui::Combo(buf, &ai, aiItems, 2)) slot.aiId = ai;
    }

    ImGui::Separator();
    std::string err;
    const bool valid = options.validate(&err);
    if (!valid) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextUnformatted(err.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("开始游戏", ImVec2(scaled(kButtonWidth, uiScale), 0.0f))) res.started = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("退出", ImVec2(scaled(kButtonWidth, uiScale), 0.0f))) res.quit = true;
    ImGui::End();
}

// 二级选图屏：预装图（带预览）/ 随机图（参数）+ 地图种子编辑 + 返回提交。
void drawMapSelectScreen(MenuState& st, SDL_Renderer* ren, Options& options, const Config& cfg,
                         const std::vector<std::string>& mapFiles, float& uiScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::max(500.0f, kBaseMapSelectWidth * uiScale), 0.0f),
                             ImGuiCond_Always);
    if (!ImGui::Begin("###map-select", nullptr,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("← 返回")) {
        commitDrafts(st, options);
        st.screen = MenuState::Screen::Main;
    }
    ImGui::Separator();

    // ---- 地图随机种子（一套：预装图山/城骰子 + 随机图生成共用；编辑/随机 → 预览失效重生成）----
    ImGui::TextUnformatted("地图随机种子");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(kButtonWidth, uiScale));
    const std::uint32_t seedBefore = st.draftSeed;
    ImGui::InputScalar("##draftseed", ImGuiDataType_U32, &st.draftSeed, nullptr, nullptr, "%u");
    ImGui::SameLine();
    if (ImGui::Button("随机##seed")) st.draftSeed = Rng::randomSeed();
    if (st.draftSeed != seedBefore) st.previews.clear();  // 种子变了 → 全部预览重生成

    ImGui::Separator();
    int mode = st.randomMode ? 1 : 0;
    ImGui::TextUnformatted("类型");
    ImGui::SameLine();
    ImGui::RadioButton("预装地图##t", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("随机地图##t", &mode, 1);
    st.randomMode = (mode == 1);

    if (!st.randomMode) {
        // ---- 预装地图列表：缩略图 + 可点选文件名（预览 = 当前种子掷出山/城后的地形图）----
        ImGui::TextUnformatted("预装地图（预览为当前种子掷出山/城后的地形图）");
        ImGui::BeginChild("##maplist", ImVec2(0.0f, 320.0f * uiScale), true);
        for (size_t i = 0; i < mapFiles.size(); ++i) {
            const std::string& file = mapFiles[i];
            if (file.rfind("data/gen_", 0) == 0) continue;  // 过滤随机图生成物（旧路径遗留；生成物已迁 userdata/maps/）
            int tw = 0, th = 0;
            SDL_Texture* t = filePreview(st, ren, cfg, file, &tw, &th);
            if (t) {
                // 预览随 UI 缩放（2026-08 修：此前恒按纹理原生尺寸显示）。
                ImGui::Image(reinterpret_cast<ImTextureID>(t),
                             ImVec2(static_cast<float>(tw) * uiScale,
                                    static_cast<float>(th) * uiScale));
                ImGui::SameLine();
            }
            char label[256];
            std::snprintf(label, sizeof(label), "%s##m%d", baseName(file), static_cast<int>(i));
            const bool selected = (st.draftFile == file);
            const float rowH = th > 0 ? static_cast<float>(th) : 0.0f;
            if (ImGui::Selectable(label, selected, 0, ImVec2(0.0f, rowH))) st.draftFile = file;
        }
        ImGui::EndChild();
    } else {
        // ---- 随机地图参数 ----
        ImGui::TextUnformatted("随机地图参数");
        // P12：密铺模式下拉（六/三角随机图；预装 BMP 恒为方形）。
        // 2026-08-16：加入全部 7 种阿基米德密铺与 7 种 Laves 密铺。
        // 2026-08-23 用户修订：半正密铺前加 "Arch" 字样；半正密铺与对应 Laves 密铺
        //（对偶）按同一顶点构型顺序排列（33336、33434、3464、3636、31212、4612、488）。
        static const char* kTilingItems[17] = {
            "正方形", "六边形", "三角形",
            "Arch 3.3.3.3.6", "Arch 3.3.4.3.4", "Arch 3.4.6.4", "Arch 3.6.3.6",
            "Arch 3.12.12", "Arch 4.6.12", "Arch 4.8.8",
            "Laves 3.3.3.3.6", "Laves 3.3.4.3.4", "Laves 3.4.6.4", "Laves 3.6.3.6",
            "Laves 3.12.12", "Laves 4.6.12", "Laves 4.8.8"};
        static const TilingType kTilingValues[17] = {
            TilingType::Square,      TilingType::Hex,        TilingType::Tri,
            TilingType::Arch33336,   TilingType::Arch33434,  TilingType::Arch3464,
            TilingType::Arch3636,    TilingType::Arch31212,  TilingType::Arch4612,
            TilingType::Arch488,
            TilingType::Laves33336,  TilingType::Laves33434, TilingType::Laves3464,
            TilingType::Laves3636,   TilingType::Laves31212, TilingType::Laves4612,
            TilingType::Laves488};
        int tilingIdx = 0;
        for (int i = 0; i < 17; ++i)
            if (st.tiling == kTilingValues[i]) tilingIdx = i;
        ImGui::SetNextItemWidth(scaled(kDropdownWidth, uiScale));
        if (ImGui::Combo("密铺模式", &tilingIdx, kTilingItems, 17)) {
            st.tiling = kTilingValues[tilingIdx];
            st.previews.clear();  // 密铺变了 → 预览失效
        }
        ImGui::SetNextItemWidth(scaled(kDropdownWidth, uiScale));
        // 三角的周期域映射已经把用户长宽转换为列/行；步进由映射输入限制统一计算。
        int wStep = 1;
        // 表驱动密铺：输入限制（算法一硬编码）——长须为 Ra 倍数、宽须为 Rb 倍数，使
        // 长*宽 = 总格数 精确守恒且映射单调（cols∝长、rows∝宽）。菜单按 Ra/Rb 设"-/+"步进。
        int Ra = 1, Rb = 1;
        const bool hasLimit = tableInputRestriction(static_cast<int>(st.tiling), Ra, Rb);
        if (hasLimit) wStep = std::lcm(wStep, Ra);
        ImGui::InputInt("长", &st.randW, wStep, 8);
        // P12 六/三角：行数强制偶数 → "宽"的 "-"/"+" 步进 2（避免点一次不变/偶奇来回跳）。
        // 表驱动：宽步进 = Rb（跳到下一个 Rb 倍数）。
        const bool needEvenRows = (st.tiling == TilingType::Hex || st.tiling == TilingType::Tri);
        const bool isTableTiling = static_cast<int>(st.tiling) > static_cast<int>(TilingType::Tri);
        int hStep = 1;
        if (hasLimit) hStep = std::lcm(needEvenRows ? 2 : 1, Rb);
        else if (needEvenRows) hStep = 2;
        else if (isTableTiling) hStep = Rb;
        ImGui::SetNextItemWidth(scaled(kDropdownWidth, uiScale));
        ImGui::InputInt("宽", &st.randH, hStep, 8);
        st.randW = std::clamp(st.randW, 32, 200);
        st.randH = std::clamp(st.randH, 32, 200);
        if (hasLimit) {
            // 长/宽四舍五入到合法区间 [32,200] 内的最近 Ra/Rb 倍数。
            const auto snapMenuDimension = [](int v, int base) {
                int r = ((v + base / 2) / base) * base;
                if (r < 32) r = ((32 + base - 1) / base) * base;
                if (r > 200) r = (200 / base) * base;
                return std::max(base, r);
            };
            st.randW = snapMenuDimension(st.randW, Ra);
            st.randH = snapMenuDimension(st.randH, Rb);
        } else if (needEvenRows) {
            st.randH = (st.randH / 2) * 2;
            if (st.randH < 32) st.randH = 32;
        } else if (isTableTiling) {
            st.randH = ((st.randH + hStep / 2) / hStep) * hStep;  // 四舍五入到 B 的倍数
            if (st.randH > 200) st.randH = (200 / hStep) * hStep;
        }
        // 强制边缘为海（在陆地占比条上方，用户要求调换位置，2026-08-06）。
        ImGui::Checkbox("强制边缘为海", &st.forceCoast);
        // 陆地占比：显式小数（1.00 = 全陆地；配合强制边缘为海 → 只有一圈海）。此前用 "%.0f%%"
        // 把 0.x 四舍五入成 "0%"/"1%" 显示错误 → 改回小数（用户反馈，2026-08-06）。
        float landRatio = 1.0f - st.seaRatio;
        if (ImGui::SliderFloat("陆地占比", &landRatio, 0.10f, 1.00f, "%.2f"))  // 下界 0.10（2026-08-06）
            st.seaRatio = 1.0f - landRatio;
        // 山/城密度：平方映射（低端精细、上限不变）。滑条位置存 st.mtnT/cityT 跨帧稳定。
        // **关键**：滑条标签必须静态（ImGui 用完整标签哈希作 ID，标签带动态值 → 拖动中 ID 每帧变
        // → ActiveId 失配 → 只能点不能拖，2026-08-06 用户反馈）；实际密度值放旁边单独文本显示。
        ImGui::SliderFloat("山密度##mtn", &st.mtnT, 0.0f, 1.0f, "");
        st.mtnDensity = squareTToDensity(st.mtnT, kMtnLo, kMtnHi);  // 每帧派生（滑条为唯一输入）
        ImGui::SameLine();
        ImGui::TextDisabled("%.4f", st.mtnDensity);

        ImGui::SliderFloat("城密度##city", &st.cityT, 0.0f, 1.0f, "");
        st.cityDensity = squareTToDensity(st.cityT, kCityLo, kCityHi);
        ImGui::SameLine();
        ImGui::TextDisabled("%.4f", st.cityDensity);
        if (ImGui::Button("生成并预览")) generateRandomMap(st, cfg, ren);

        int tw = 0, th = 0;
        if (SDL_Texture* t = st.previews.lookup(st.genPath, &tw, &th)) {
            // 预览随 UI 缩放（2026-08 修：此前恒按纹理原生尺寸显示）。
            ImGui::Image(reinterpret_cast<ImTextureID>(t),
                         ImVec2(static_cast<float>(tw) * uiScale, static_cast<float>(th) * uiScale));
            // 2026-08 斜周期（33336 系）：预览为"旋转到常规矩形"后的地图（非平行四边形原位）。
            if (TilingGeom{st.tiling, st.randW, st.randH}.hasSkewedPeriod())
                ImGui::TextDisabled("预览的是旋转后的地图");
        }
        if (!st.genError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextUnformatted(st.genError.c_str());
            ImGui::PopStyleColor();
        }
        if (!st.genPath.empty()) ImGui::TextUnformatted(st.genPath.c_str());
    }

    ImGui::Separator();
    char sbuf[64];
    std::snprintf(sbuf, sizeof(sbuf), "主随机种子（只读）: %u", st.mainSeed);
    ImGui::TextUnformatted(sbuf);
    ImGui::End();
}

}  // namespace

// ---- PreviewCache ----

void PreviewCache::release() {
    for (auto& e : items) {
        if (e.tex) SDL_DestroyTexture(e.tex);
    }
    items.clear();
}

void PreviewCache::clear() {
    release();
}

SDL_Texture* PreviewCache::get(SDL_Renderer* ren, const std::string& key, const Map& map,
                               int previewW, int* outW, int* outH) {
    // 命中缓存直接返回（w/h 以首次渲染为准）。
    for (auto& e : items) {
        if (e.key == key) {
            if (outW) *outW = e.w;
            if (outH) *outH = e.h;
            return e.tex;
        }
    }
    Entry e;
    e.key = key;
    e.tex = render::renderMapPreview(ren, map, previewW);
    if (!e.tex) return nullptr;
    // 预览显示尺寸 = 实际纹理尺寸（2026-08：斜周期 33336 系预览为"旋转到常规矩形"，
    // 其纹理不是按 worldWidth/Height AABB 生成的，故不再估算纵横比，直接查纹理实际像素）。
    {
        Uint32 fmt = 0;
        int acc = 0;
        SDL_QueryTexture(e.tex, &fmt, &acc, &e.w, &e.h);
        e.w = std::max(1, e.w);
        e.h = std::max(1, e.h);
    }
    items.push_back(e);
    if (outW) *outW = e.w;
    if (outH) *outH = e.h;
    return e.tex;
}

// 查询缓存而不渲染（供随机图/预装图显示阶段）。返回 nullptr = 未生成/已失效。
SDL_Texture* PreviewCache::lookup(const std::string& key, int* outW, int* outH) {
    for (auto& e : items) {
        if (e.key == key) {
            if (outW) *outW = e.w;
            if (outH) *outH = e.h;
            return e.tex;
        }
    }
    return nullptr;
}

std::vector<std::string> enumerateMapFiles(const std::string& dataDir) {
    std::vector<std::string> out;
#ifdef _WIN32
    const std::string pattern = dataDir + "/*.bmp";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        spdlog::warn("map scan '{}' failed (FindFirstFileA)", pattern);
        return out;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // 统一正斜杠（与 config.json 的 map.file 一致，如 data/map_bigIslands.bmp）。
            out.push_back(dataDir + "/" + fd.cFileName);
        }
    } while (FindNextFileA(hFind, &fd) != 0);
    FindClose(hFind);
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(dataDir, ec);
    const fs::directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        const auto& entry = *it;
        if (!entry.is_regular_file(ec) && !ec) continue;
        if (entry.path().extension() == ".bmp")
            out.push_back(dataDir + "/" + entry.path().filename().string());
    }
    if (ec)
        spdlog::warn("map scan '{}' failed: {}", dataDir, ec.message());
#endif
    std::sort(out.begin(), out.end());
    return out;
}

MenuResult drawMenu(MenuState& st, SDL_Renderer* ren, Options& options, const Config& cfg,
                    const std::vector<std::string>& mapFiles, float& uiScale) {
    MenuResult res;
    if (st.screen == MenuState::Screen::MapSelect) {
        drawMapSelectScreen(st, ren, options, cfg, mapFiles, uiScale);
    } else {
        drawMainScreen(st, options, cfg, mapFiles, uiScale, res);
    }

    // ---- 独立的 UI 缩放条（左下角；左端中部锚定，拖拽时不会因缩放而"逃逸"鼠标）----
    // 单独一个无装饰小窗，避免随主菜单缩放/移动导致滑条跑开。每帧按当前 uiScale 反算
    // 窗口位置，使滑条【左端中部】固定在屏幕 (anchorX, anchorY)，缩放只向上下/右扩展。
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float frameH = ImGui::GetFrameHeight();   // 滑条高度（随字形缩放）
        const ImVec2 pad = ImGui::GetStyle().WindowPadding;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float labelW = ImGui::CalcTextSize("UI缩放").x;
        const float sliderW = scaled(kSliderWidth, uiScale);  // 长度随缩放倍率（共享基准 220）
        const float anchorX = 8.0f;                 // 滑条左端 x（固定）
        const float anchorY = display.y - 24.0f;    // 滑条垂直中部 y（固定）
        // 窗口左上角：使滑条左端 = anchorX、滑条垂直中部 = anchorY。
        ImGui::SetNextWindowPos(ImVec2(anchorX - pad.x, anchorY - pad.y - frameH * 0.5f));
        // 窗口宽恰好容纳滑条+标签（+4 缓冲防裁切）；高 0 = 随内容。
        ImGui::SetNextWindowSize(
            ImVec2(pad.x * 2.0f + sliderW + gap + labelW + 4.0f, 0.0f));
        ImGui::Begin("###ui-scale", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderFloat("UI缩放", &uiScale, kMinUiScale, kMaxUiScale, "%.2f");
        ImGui::End();
    }

    return res;
}

}  // namespace lw::ui
