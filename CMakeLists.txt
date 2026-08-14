cmake_minimum_required(VERSION 3.20)
project(landwar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 依赖 (MSYS2 UCRT64, docs/environment-setup.md 方案A A.1)
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(EnTT REQUIRED)

# ---------------------------------------------------------------------------
# Dear ImGui（本地 _deps/imgui-src，非 MSYS2 仓库；Phase 8 接入 UI 时使用）
# 注：本地目录未锁版本/URL（离线构建优先）；升级时整体替换 _deps/imgui-src。
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/_deps/imgui-src
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# ---------------------------------------------------------------------------
# 工程源码统一警告（2026-08 工程改进：GLOB_RECURSE → 显式源列表；
# 新文件须手动加入对应列表，杜绝"加了文件忘进工程"与增量扫描漂移）。
# 警告仅作用于本项目目标（第三方 imgui/entt/spdlog 不动）。
# ---------------------------------------------------------------------------
function(landwar_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 模拟核心库（纯逻辑，无渲染/平台依赖）— 无头模式与单元测试共用
# ---------------------------------------------------------------------------
add_library(landwar_core STATIC
    src/core/Config.cpp
    src/core/MathUtil.cpp
    src/core/Options.cpp
    src/core/Paths.cpp
    src/core/Random.cpp
    src/core/Simulation.cpp
    src/world/Faction.cpp
    src/world/Map.cpp
    src/world/MapGenerator.cpp
    src/sim/Buff.cpp
    src/sim/SpatialHash.cpp
    src/sim/ai/ProductionAI.cpp
    src/sim/systems/CapitalSystem.cpp
    src/sim/systems/CombatSystem.cpp
    src/sim/systems/DeathSystem.cpp
    src/sim/systems/EconomySystem.cpp
    src/sim/systems/EffectSystem.cpp
    src/sim/systems/MovementSystem.cpp
    src/sim/systems/ProductionSystem.cpp
    src/sim/systems/ProjectileSystem.cpp
    src/sim/systems/SpawnSystem.cpp
    src/sim/systems/TechSystem.cpp
    src/sim/systems/UnitActionSystem.cpp)
target_include_directories(landwar_core PUBLIC src)
target_link_libraries(landwar_core PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    EnTT::EnTT)
landwar_warnings(landwar_core)

# ---------------------------------------------------------------------------
# 存档/回放（无头 CLI + 快照序列化；EXE 与测试共用）
# ---------------------------------------------------------------------------
add_library(landwar_replay STATIC
    src/replay/Cli.cpp
    src/replay/Headless.cpp
    src/replay/Snapshot.cpp)
target_include_directories(landwar_replay PUBLIC src)
target_link_libraries(landwar_replay PUBLIC landwar_core)
landwar_warnings(landwar_replay)

# ---------------------------------------------------------------------------
# 输入层（Phase 8：仅 SDL 事件翻译，无渲染/模拟依赖 → 可独立单测）
# ---------------------------------------------------------------------------
add_library(landwar_input STATIC src/app/InputManager.cpp)
target_include_directories(landwar_input PUBLIC src)
target_link_libraries(landwar_input PUBLIC SDL2::SDL2 EnTT::EnTT)
landwar_warnings(landwar_input)

# ---------------------------------------------------------------------------
# 渲染层（P13 拆分：渲染器 .cpp 独立成库，EXE 与单元测试共用 →
# render/CityRenderer::compute 纯几何可单测，无需 SDL 渲染器）
# ---------------------------------------------------------------------------
add_library(landwar_render STATIC
    src/render/ArmyRenderer.cpp
    src/render/CityMarkerRenderer.cpp
    src/render/CityRenderer.cpp
    src/render/EffectRenderer.cpp
    src/render/MapPreview.cpp
    src/render/MapRenderer.cpp
    src/render/ProjectileRenderer.cpp
    src/render/Renderer.cpp
    src/render/SpriteSheet.cpp
    src/render/TintCache.cpp
    src/render/TwoToneTintCache.cpp)
target_include_directories(landwar_render PUBLIC src)
target_link_libraries(landwar_render PUBLIC
    landwar_core
    SDL2::SDL2
    SDL2_image::SDL2_image)
landwar_warnings(landwar_render)

# ---------------------------------------------------------------------------
# 应用（窗口/渲染/UI；InputManager 已拆到 landwar_input，渲染器已拆到 landwar_render）
# ---------------------------------------------------------------------------
add_executable(landwar
    src/main.cpp
    src/app/Application.cpp
    src/ui/DebugPanel.cpp
    src/ui/ImGuiSetup.cpp
    src/ui/Menu.cpp
    src/ui/UiText.cpp
    src/ui/panels/MessagePanel.cpp
    src/ui/panels/PanelManager.cpp
    src/ui/panels/StatsPanel.cpp
    src/ui/panels/TechPanel.cpp)
target_include_directories(landwar PRIVATE src)
target_link_libraries(landwar PRIVATE
    landwar_core
    landwar_replay
    landwar_input
    landwar_render
    SDL2::SDL2
    SDL2_image::SDL2_image
    imgui)
landwar_warnings(landwar)

# ---------------------------------------------------------------------------
# 单元测试（gtest, MSYS2: mingw-w64-ucrt-x86_64-gtest）
# 2026-08：新增 test_baseline.cpp（20k tick 确定性基线 hash 锁定）。
# ---------------------------------------------------------------------------
find_package(GTest REQUIRED)
add_executable(landwar_tests
    tests/TestUtil.h
    tests/test_army.cpp
    tests/test_baseline.cpp
    tests/test_buff.cpp
    tests/test_camera.cpp
    tests/test_capital.cpp
    tests/test_city.cpp
    tests/test_city_marker.cpp
    tests/test_city_render.cpp
    tests/test_config.cpp
    tests/test_economy.cpp
    tests/test_effect.cpp
    tests/test_events.cpp
    tests/test_faction.cpp
    tests/test_fixed_timestep.cpp
    tests/test_input.cpp
    tests/test_main.cpp
    tests/test_map.cpp
    tests/test_mapgen.cpp
    tests/test_math.cpp
    tests/test_mountain.cpp
    tests/test_options.cpp
    tests/test_panels.cpp
    tests/test_perf.cpp
    tests/test_player.cpp
    tests/test_projectile.cpp
    tests/test_rng.cpp
    tests/test_sim.cpp
    tests/test_snapshot.cpp
    tests/test_stats.cpp
    tests/test_tech.cpp
    tests/test_tint.cpp
    tests/test_wrapdraw.cpp)
target_include_directories(landwar_tests PRIVATE src)
target_link_libraries(landwar_tests PRIVATE
    landwar_core
    landwar_replay
    landwar_input
    landwar_render
    GTest::gtest
    GTest::gtest_main)
landwar_warnings(landwar_tests)
enable_testing()
add_test(NAME landwar_tests COMMAND landwar_tests)
set_tests_properties(landwar_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
