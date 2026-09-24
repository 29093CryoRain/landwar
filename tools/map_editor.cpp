#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_image.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/Config.h"
#include "core/Paths.h"
#include "editor/MapEditorModel.h"
#include "render/Camera.h"
#include "render/CityRenderer.h"
#include "render/MapRenderer.h"
#include "render/Renderer.h"
#include "ui/ImGuiSetup.h"
#include "world/Map.h"
#include "world/tiling/Tiling.h"

namespace {

constexpr int kWindowWidth = 1440;
constexpr int kWindowHeight = 900;
constexpr int kPanelWidth = 300;
constexpr double kGridLineMinCellPx = 32.0;

const char* terrainName(lw::MapTerrain terrain) {
    switch (terrain) {
        case lw::MapTerrain::Sea: return "海";
        case lw::MapTerrain::Land: return "陆";
        case lw::MapTerrain::Mountain: return "山";
        case lw::MapTerrain::City: return "城";
    }
    return "海";
}

const char* tilingNameZh(lw::TilingType tiling) {
    switch (tiling) {
        case lw::TilingType::Square: return "正方形";
        case lw::TilingType::Hex: return "正六边形";
        case lw::TilingType::Tri: return "正三角形";
        case lw::TilingType::Arch33336: return "Arch 3.3.3.3.6";
        case lw::TilingType::Arch33434: return "Arch 3.3.4.3.4";
        case lw::TilingType::Arch3464: return "Arch 3.4.6.4";
        case lw::TilingType::Arch3636: return "Arch 3.6.3.6";
        case lw::TilingType::Arch31212: return "Arch 3.12.12";
        case lw::TilingType::Arch4612: return "Arch 4.6.12";
        case lw::TilingType::Arch488: return "Arch 4.8.8";
        case lw::TilingType::Laves3636: return "Laves 3.6.3.6";
        case lw::TilingType::Laves31212: return "Laves 3.12.12";
        case lw::TilingType::Laves4612: return "Laves 4.6.12";
        case lw::TilingType::Laves488: return "Laves 4.8.8";
        case lw::TilingType::Laves33434: return "Laves 3.3.4.3.4";
        case lw::TilingType::Laves33336: return "Laves 3.3.3.3.6";
        case lw::TilingType::Laves3464: return "Laves 3.4.6.4";
    }
    return "未知密铺";
}

bool isMapPointer(int x, int y) {
    return x >= kPanelWidth && x < kWindowWidth && y >= 0 && y < kWindowHeight &&
           !ImGui::GetIO().WantCaptureMouse;
}

int cityMarkCount(const lw::editor::MapEditorModel& model) {
    int count = 0;
    for (int index = 0; index < model.cellCount(); ++index)
        if (model.cityMarked(index)) ++count;
    return count;
}

std::vector<int> brushCells(const lw::editor::MapEditorModel& model, int center, int size) {
    std::vector<int> result;
    if (center < 0 || center >= model.cellCount()) return result;
    if (size <= 1) return {center};
    const int radius = std::max(0, size - 1);
    std::vector<int> distance(static_cast<std::size_t>(model.cellCount()), -1);
    std::vector<int> queue{center};
    distance[static_cast<std::size_t>(center)] = 0;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int current = queue[head];
        const int currentDistance = distance[static_cast<std::size_t>(current)];
        result.push_back(current);
        if (currentDistance >= radius) continue;
        const int count = model.geometry().neighborCount(current);
        for (int k = 0; k < count; ++k) {
            const int next = model.geometry().neighbor(current, k);
            if (next < 0 || distance[static_cast<std::size_t>(next)] >= 0) continue;
            distance[static_cast<std::size_t>(next)] = currentDistance + 1;
            queue.push_back(next);
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    lw::Config config = lw::Config::loadFromFile(lw::kDefaultConfigPath);
    lw::editor::MapEditorModel model;
    model.setCityConfig(config.city);
    std::string error;
    const std::string inputPath = argc > 1
                                      ? argv[1]
                                      : (!config.map.file.empty() ? config.map.file
                                                                   : lw::kDefaultEditorMapPath);
    if (!model.loadFromFile(inputPath, &error)) {
        spdlog::warn("map_editor: {}", error);
        int cols = config.map.width;
        int rows = config.map.height;
        lw::chooseTableDomain(static_cast<int>(config.map.tilingType()), cols, rows, cols, rows);
        model.configureCanonical(config.map.tilingType(), cols, rows, &error);
        lw::MapDefinition blank = model.toDefinition();
        blank.terrain.assign(static_cast<std::size_t>(model.cellCount()), lw::MapTerrain::Land);
        if (!model.loadDefinition(blank, &error)) {
            spdlog::critical("map_editor: cannot create map: {}", error);
            return 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        spdlog::critical("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        spdlog::critical("IMG_Init PNG failed: {}", IMG_GetError());
        SDL_Quit();
        return 1;
    }
    if (!lw::ensureDirExists(lw::kMapDataDir)) {
        spdlog::warn("map_editor: map directory '{}' is unavailable", lw::kMapDataDir);
    }
    SDL_Window* window = SDL_CreateWindow("领土战争地图编辑器", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        spdlog::critical("SDL_CreateWindow failed: {}", SDL_GetError());
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        spdlog::critical("SDL_CreateRenderer failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(renderer, kWindowWidth, kWindowHeight);
    if (!lw::ui::initImGui(window, renderer)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    {
        lw::math::ScreenTransform transform{24.0, kPanelWidth, model.geometry().worldHeight()};
        lw::render::Camera camera;
        camera.configure(transform, kWindowWidth, kWindowHeight, model.geometry().worldWidth(),
                         model.geometry().worldHeight(), kPanelWidth, 0,
                         kWindowWidth - kPanelWidth, kWindowHeight);
        lw::render::CityRenderer cityRenderer(renderer, camera);
        lw::Config editorConfig = config;
        if (!editorConfig.factions.empty()) {
            editorConfig.factions[0].color = {232, 216, 120};
            editorConfig.factions[0].secondary = {232, 216, 120};
        }
        cityRenderer.bake(editorConfig);
        lw::render::MapRenderer mapRenderer(renderer, camera, config.render.mountain, config.render.river);
        const std::vector<std::array<int, 3>> editorColors = {
            {0, 0, 0}, {104, 133, 94}, {133, 116, 92}, {232, 195, 64}};
        mapRenderer.bake(editorColors);
        lw::Map previewMap;
        lw::render::CityRenderer::Frame cityFrame;
        bool cityFrameDirty = true;
        double cityFrameZoom = -1.0;
        double cityFramePanX = 0.0;
        double cityFramePanY = 0.0;
        bool previewDirty = true;
        bool quit = false;
        bool leftPainting = false;
        bool rightPanning = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
        int selectedTiling = static_cast<int>(model.tiling());
        int inputLength = model.cols() > 0 ? model.cols() : config.map.width;
        int inputWidth = model.rows() > 0 ? model.rows() : config.map.height;
        int paintChoice = static_cast<int>(lw::MapTerrain::Land);
        int brushMode = 0;
        int brushSize = 1;
        bool eraseCityMarks = false;
        // 河流工具（§8.2）：0=关、1=画河、2=擦河。独立于地形 paintChoice（河是边级 +
        // 需要"最近边"命中），非 0 时鼠标编辑走河逻辑、不走地形笔刷。
        int riverTool = 0;
        bool savePopup = false;
        bool largeOperationPopup = false;
        bool saveResult = false;
        std::string saveError;
        char savePath[512] = {};
        std::strncpy(savePath, lw::kDefaultEditorMapPath.c_str(), sizeof(savePath) - 1);
        char loadPath[512] = {};
        std::strncpy(loadPath, inputPath.c_str(), sizeof(loadPath) - 1);

        const auto resetCameraForModel = [&]() {
            transform.mapHeight = model.geometry().worldHeight();
            camera.configure(transform, kWindowWidth, kWindowHeight,
                             model.geometry().worldWidth(), model.geometry().worldHeight(),
                             kPanelWidth, 0, kWindowWidth - kPanelWidth, kWindowHeight);
            camera.reset();
            selectedTiling = static_cast<int>(model.tiling());
            previewDirty = true;
            cityFrameDirty = true;
        };
        const auto snapDimension = [](int value, int multiple) {
            const int step = std::max(1, multiple);
            value = std::clamp(value, 32, 200);
            int snapped = ((value + step / 2) / step) * step;
            if (snapped < 32) snapped = ((32 + step - 1) / step) * step;
            if (snapped > 200) snapped = (200 / step) * step;
            return std::max(step, snapped);
        };
        const auto configureFromInputDimensions = [&]() {
            int ra = 1, rb = 1;
            const bool restricted = lw::tableInputRestriction(selectedTiling, ra, rb);
            if (restricted) {
                inputLength = snapDimension(inputLength, ra);
                inputWidth = snapDimension(inputWidth, rb);
            } else {
                inputLength = std::clamp(inputLength, 32, 200);
                inputWidth = std::clamp(inputWidth, 32, 200);
            }
            int canonicalCols = inputLength;
            int canonicalRows = inputWidth;
            lw::chooseTableDomain(selectedTiling, inputLength, inputWidth, canonicalCols,
                                  canonicalRows);
            if (model.tiling() == static_cast<lw::TilingType>(selectedTiling)
                && model.cols() == canonicalCols && model.rows() == canonicalRows)
                return;
            std::string configureError;
            if (!model.configureCanonical(static_cast<lw::TilingType>(selectedTiling),
                                          canonicalCols, canonicalRows, &configureError)) {
                spdlog::error("map_editor: {}", configureError);
                return;
            }
            resetCameraForModel();
        };
        std::string loadError;
        const auto load = [&]() {
            loadError.clear();
            std::string error;
            if (!model.loadFromFile(loadPath, &error)) {
                loadError = error;
                return;
            }
            inputLength = model.cols();
            inputWidth = model.rows();
            saveError.clear();
            resetCameraForModel();
        };
        const auto updatePreview = [&]() {
            const lw::MapDefinition definition = model.toDefinition();
            previewMap.configureCanonical(definition.tiling, definition.cols, definition.rows);
            previewMap.setTerrain(config.terrain);
            previewMap.setCityConfig(config.city);
            std::string previewError;
            if (!previewMap.loadFromDefinition(definition, &previewError))
                spdlog::warn("map_editor preview: {}", previewError);
            for (int index = 0; index < previewMap.cellCount(); ++index) {
                auto& cell = previewMap.atIndex(index);
                switch (model.terrainAt(index)) {
                    case lw::MapTerrain::Sea:
                        cell.land = false;
                        cell.mountain = false;
                        cell.belongi = 0;
                        break;
                    case lw::MapTerrain::Land:
                        cell.land = true;
                        cell.mountain = false;
                        cell.belongi = 1;
                        break;
                    case lw::MapTerrain::Mountain:
                        cell.land = true;
                        cell.mountain = true;
                        cell.belongi = 2;
                        break;
                    case lw::MapTerrain::City:
                        cell.land = true;
                        cell.mountain = model.mountainMarked(index);
                        cell.belongi = 3;
                        break;
                }
            }
            previewDirty = false;
            cityFrameDirty = true;
        };
        const auto syncPreviewCell = [&](int index) {
            if (previewMap.cellCount() != model.cellCount() || index < 0
                || index >= previewMap.cellCount())
                return false;
            auto& cell = previewMap.atIndex(index);
            switch (model.terrainAt(index)) {
                case lw::MapTerrain::Sea:
                    cell.land = false;
                    cell.mountain = false;
                    cell.cityId = -1;
                    cell.belongi = 0;
                    break;
                case lw::MapTerrain::Land:
                    cell.land = true;
                    cell.mountain = false;
                    cell.cityId = -1;
                    cell.belongi = 1;
                    break;
                case lw::MapTerrain::Mountain:
                    cell.land = true;
                    cell.mountain = true;
                    cell.cityId = -1;
                    cell.belongi = 2;
                    break;
                case lw::MapTerrain::City:
                    cell.land = true;
                    cell.mountain = model.mountainMarked(index);
                    cell.cityId = -1;
                    cell.belongi = 3;
                    break;
            }
            return true;
        };
        const auto editAt = [&](int x, int y) {
            if (!isMapPointer(x, y)) return;
            const double worldX = camera.toWorldX(x);
            const double worldY = camera.toWorldY(y);
            const int index = model.geometry().worldToCell(worldX, worldY);
            if (index < 0) return;
            if (riverTool != 0) {
                // 河：最近边命中（跳过地图边界边与过远命中）→ 一次可撤销操作。
                // 河是稀疏数据 → 直接全量重建预览（省掉边级的增量同步）。
                const int changed = riverTool == 1 ? model.paintRiverNearest(index, worldX, worldY)
                                                   : model.eraseRiverNearest(index, worldX, worldY);
                if (changed > 0) previewDirty = true;
                return;
            }
            const bool cityEdit = paintChoice == 3;
            const std::vector<int> affected = brushMode == 0 && !cityEdit
                                                  ? brushCells(model, index, brushSize)
                                                  : std::vector<int>{};
            const bool removesCity = !cityEdit && brushMode == 0 && std::any_of(
                affected.begin(), affected.end(),
                [&](int cell) { return model.cityMarked(cell); });
            bool changed = false;
            if (cityEdit) {
                if (brushMode == 1) {
                    changed = model.floodFillCityMarks(index, !eraseCityMarks);
                } else {
                    for (const int cell : brushCells(model, index, brushSize))
                        if (model.setCityMark(cell, !eraseCityMarks)) changed = true;
                }
            } else {
                if (brushMode == 1) {
                    changed = model.floodFillTerrain(
                        index, static_cast<lw::MapTerrain>(paintChoice));
                } else {
                    for (const int cell : affected)
                        if (model.paintCell(cell, static_cast<lw::MapTerrain>(paintChoice)))
                            changed = true;
                }
            }
            if (changed > 0) {
                const bool fullPreview = cityEdit || removesCity || brushMode == 1;
                if (fullPreview) {
                    previewDirty = true;
                } else {
                    for (const int cell : affected)
                        if (!syncPreviewCell(cell)) previewDirty = true;
                }
                if (model.lastCityMarkIncrease() > 30) largeOperationPopup = true;
            }
        };
        const auto save = [&]() {
            saveError.clear();
            saveResult = model.saveToFile(savePath, &saveError);
            if (!saveResult) spdlog::error("map_editor save: {}", saveError);
            savePopup = false;
            ImGui::CloseCurrentPopup();
        };

        while (!quit) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                lw::ui::processImGuiEvent(event);
                if (event.type == SDL_QUIT) quit = true;
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_z &&
                    (event.key.keysym.mod & KMOD_CTRL) && !ImGui::GetIO().WantCaptureKeyboard) {
                    if (model.undo()) previewDirty = true;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        leftPainting = isMapPointer(event.button.x, event.button.y);
                        if (leftPainting) {
                            model.beginBatch();
                            editAt(event.button.x, event.button.y);
                        }
                    } else if (event.button.button == SDL_BUTTON_RIGHT &&
                               isMapPointer(event.button.x, event.button.y)) {
                        rightPanning = true;
                        lastMouseX = event.button.x;
                        lastMouseY = event.button.y;
                    }
                } else if (event.type == SDL_MOUSEBUTTONUP) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        leftPainting = false;
                        if (model.endBatch()) {
                            previewDirty = true;
                            if (model.lastCityMarkIncrease() > 30) largeOperationPopup = true;
                        }
                    }
                    if (event.button.button == SDL_BUTTON_RIGHT) rightPanning = false;
                } else if (event.type == SDL_MOUSEMOTION) {
                    if (rightPanning) {
                        camera.pan(event.motion.x - lastMouseX, event.motion.y - lastMouseY);
                        cityFrameDirty = true;
                        lastMouseX = event.motion.x;
                        lastMouseY = event.motion.y;
                    } else if (leftPainting) {
                        editAt(event.motion.x, event.motion.y);
                    }
                } else if (event.type == SDL_MOUSEWHEEL && !ImGui::GetIO().WantCaptureMouse) {
                    int mouseX = 0, mouseY = 0;
                    SDL_GetMouseState(&mouseX, &mouseY);
                    camera.zoomAt(mouseX, mouseY, std::pow(1.12, event.wheel.y));
                    cityFrameDirty = true;
                }
            }

            if (previewDirty) updatePreview();
            if (cityFrameDirty || cityFrameZoom != camera.zoom() || cityFramePanX != camera.panX()
                || cityFramePanY != camera.panY()) {
                cityFrame = cityRenderer.compute(previewMap, config.render);
                cityFrameDirty = false;
                cityFrameZoom = camera.zoom();
                cityFramePanX = camera.panX();
                cityFramePanY = camera.panY();
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            mapRenderer.draw(previewMap, editorColors);
            // 非法河（临海/边界）用红色可见标记：合法河由 MapRenderer 画黑线，
            // 但非法河不导出（toDefinition 过滤）→ 不进 previewMap，必须在此单独提示。
            if (model.hasRiverViolations()) {
                lw::render::Renderer riverRenderer(renderer);
                const lw::render::Camera& rcam = camera;
                for (const lw::MapEdgeRef& ref : model.riverViolations()) {
                    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
                    if (!model.geometry().cellEdge(ref.cell, ref.edge, x0, y0, x1, y1)) continue;
                    riverRenderer.fillThickSegment(rcam.toScreenXi(x0), rcam.toScreenYi(y0),
                                                   rcam.toScreenXi(x1), rcam.toScreenYi(y1), 4,
                                                   SDL_Color{220, 40, 40, 255});
                }
            }
            if (camera.cellPx() >= kGridLineMinCellPx) mapRenderer.drawGrid(previewMap);
            cityRenderer.drawFrame(cityFrame, config.render);

            lw::ui::beginImGuiFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kPanelWidth),
                                            static_cast<float>(kWindowHeight)), ImGuiCond_Always);
            ImGui::Begin("地图编辑器###map-editor", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse);
            ImGui::TextUnformatted("地图编辑器");
            ImGui::Separator();
            ImGui::TextUnformatted("密铺模式");
            if (ImGui::BeginCombo("##tiling", tilingNameZh(model.tiling()))) {
                for (int value = 0; value < lw::kTilingTypeCount; ++value) {
                    const bool selected = value == selectedTiling;
                    if (ImGui::Selectable(tilingNameZh(static_cast<lw::TilingType>(value)), selected)) {
                        selectedTiling = value;
                        configureFromInputDimensions();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            int ra = 1, rb = 1;
            const bool restricted = lw::tableInputRestriction(selectedTiling, ra, rb);
            ImGui::SetNextItemWidth(110.0f);
            const bool lengthChanged = ImGui::InputInt("长", &inputLength, restricted ? ra : 1, 8);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            const bool widthChanged = ImGui::InputInt("宽", &inputWidth, restricted ? rb : 1, 8);
            if (lengthChanged || widthChanged) configureFromInputDimensions();
            ImGui::Text("周期域：%d × %d", model.cols(), model.rows());
            ImGui::Separator();
            ImGui::TextUnformatted("填充内容");
            for (int value = 0; value < 4; ++value) {
                if (value > 0) ImGui::SameLine();
                const char* label = value < 3 ? terrainName(static_cast<lw::MapTerrain>(value)) : "城";
                ImGui::RadioButton(label, &paintChoice, value);
            }
            if (paintChoice == 3) ImGui::Checkbox("擦除城市标记", &eraseCityMarks);
            ImGui::TextUnformatted("河流（边级工具，独立于上面的填充内容）");
            ImGui::RadioButton("关##river", &riverTool, 0);
            ImGui::SameLine();
            ImGui::RadioButton("画河", &riverTool, 1);
            ImGui::SameLine();
            ImGui::RadioButton("擦河", &riverTool, 2);
            if (riverTool != 0)
                ImGui::TextUnformatted("提示：点击/拖拽选最近的边；河边两侧必须都是陆地。");
            ImGui::TextUnformatted("画笔");
            ImGui::RadioButton("单点笔刷", &brushMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("填充笔刷", &brushMode, 1);
            ImGui::BeginDisabled(brushMode == 1);
            ImGui::SetNextItemWidth(125.0f);
            ImGui::SliderInt("笔刷大小", &brushSize, 1, 12);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55.0f);
            ImGui::InputInt("##brush-size-input", &brushSize, 1, 2);
            brushSize = std::clamp(brushSize, 1, 12);
            ImGui::EndDisabled();
            if (ImGui::Button("撤销 (Ctrl+Z)")) {
                if (model.undo()) previewDirty = true;
            }
            ImGui::Separator();
            ImGui::Text("格子数：%d", model.cellCount());
            ImGui::Text("城市标记：%d", cityMarkCount(model));
            ImGui::Text("已分派城市：%d", static_cast<int>(model.resolvedCities().size()));
            if (model.hasUnresolvedMarks()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.2f, 1.0f));
                ImGui::Text("未分派城市格：%d", model.unresolvedMarkCount());
                ImGui::PopStyleColor();
            }
            if (model.hasMountainCoastViolations()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.2f, 1.0f));
                ImGui::Text("临海山地：%d", model.mountainCoastViolationCount());
                ImGui::PopStyleColor();
            }
            ImGui::Text("河段：%d", static_cast<int>(model.rivers().size()));
            if (model.hasRiverViolations()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.2f, 1.0f));
                ImGui::Text("临海河（不会保存）：%d", model.riverSeaViolationCount());
                ImGui::PopStyleColor();
            }
            ImGui::Separator();
            ImGui::InputText("读取路径", loadPath, sizeof(loadPath));
            if (ImGui::Button("读取地图")) load();
            if (!loadError.empty()) ImGui::TextWrapped("读取失败：%s", loadError.c_str());
            ImGui::Separator();
            ImGui::InputText("保存路径", savePath, sizeof(savePath));
            if (ImGui::Button("保存地图")) {
                savePopup = model.hasUnresolvedMarks() || model.hasMountainCoastViolations() ||
                            model.hasRiverViolations();
                if (!savePopup) save();
            }
            if (!saveError.empty()) ImGui::TextWrapped("保存失败：%s", saveError.c_str());
            ImGui::End();

            if (savePopup) ImGui::OpenPopup("确认保存");
            if (ImGui::BeginPopupModal("确认保存", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("保存当前地图？");
                if (model.hasUnresolvedMarks())
                    ImGui::TextWrapped("未分派城市格将保留为城格颜色，但不会生成城市记录。");
                if (model.hasMountainCoastViolations())
                    ImGui::TextWrapped("存在临海山地，请确认保存。");
                if (model.hasRiverViolations())
                    ImGui::TextWrapped("存在 %d 条临海/边界河，它们不会被保存（会被丢弃）。",
                                       model.riverSeaViolationCount());
                if (ImGui::Button("确定", ImVec2(120.0f, 0.0f))) save();
                ImGui::SameLine();
                if (ImGui::Button("取消", ImVec2(120.0f, 0.0f))) {
                    savePopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (largeOperationPopup) ImGui::OpenPopup("确认大范围城市操作");
            if (ImGui::BeginPopupModal("确认大范围城市操作", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("本次操作增加了超过 30 个城市格。");
                if (ImGui::Button("确定操作", ImVec2(120.0f, 0.0f))) {
                    largeOperationPopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("取消操作", ImVec2(120.0f, 0.0f))) {
                    if (model.undo()) previewDirty = true;
                    largeOperationPopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            lw::ui::renderImGui(renderer);
            SDL_RenderPresent(renderer);
        }
    }

    lw::ui::shutdownImGui();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
