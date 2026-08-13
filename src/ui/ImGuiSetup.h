// ImGuiSetup.h — ImGui + SDL2 + SDLRenderer2 后端初始化（翻新计划 Phase 7，§3.1 ui/）。
// 复用 scaffold 的 ImGui 接入序列（environment-setup.md）；Phase 8 的 DebugPanel 依赖本模块。
// 生命周期：initImGui 在 SDL 窗口/渲染器创建后调用；shutdownImGui 在销毁渲染器之前调用。
// 注意：SDL_RenderSetLogicalSize 下，坐标空间已由 imgui_impl_sdl2.cpp 的本地补丁
// （ApplyLogicalSize：DisplaySize/鼠标统一到逻辑坐标）处理，见该文件头注。
#pragma once

#include <SDL.h>

namespace lw::ui {

// 创建 ImGui 上下文并初始化 SDL2 / SDLRenderer2 后端。失败返回 false（已记错误日志）。
bool initImGui(SDL_Window* win, SDL_Renderer* ren);

// 每帧开头调用（SDL 绘制命令之后、构建 ImGui 窗口之前）：ImGui_Impl*_NewFrame + NewFrame。
void beginImGuiFrame();

// 每帧末尾调用：Render + 将 ImGui 几何数据提交到 SDL 渲染器。
void renderImGui(SDL_Renderer* ren);

// SDL 事件透传给 ImGui（须在 Application 事件循环中逐事件调用）。
void processImGuiEvent(const SDL_Event& ev);

// 销毁 ImGui 上下文（先于 SDL_DestroyRenderer）。
void shutdownImGui();

}  // namespace lw::ui
