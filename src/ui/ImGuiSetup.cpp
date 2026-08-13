// ImGuiSetup.cpp — ImGui + SDL2 + SDLRenderer2 后端实现（翻新计划 Phase 7）。
#include "ui/ImGuiSetup.h"

#include <spdlog/spdlog.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

namespace lw::ui {

bool initImGui(SDL_Window* win, SDL_Renderer* ren) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // 不写 imgui.ini（避免工作目录垃圾）

    // 中文字体：ImGui 默认字体（ProggyClean）只有 ASCII 字形 → 中文显示为 '?'。
    // 加载系统中文字体作为默认字体（含全量常用汉字表），失败回退默认并记警告。
    // 首个 AddFont 即默认字体；SDLRenderer2 后端在首次 NewFrame 时构建字体图集纹理。
    static const char* kCjkFontCandidates[] = {
        "C:/Windows/Fonts/msyh.ttc",    // 微软雅黑
        "C:/Windows/Fonts/simhei.ttf",  // 黑体
        "C:/Windows/Fonts/simsun.ttc",  // 宋体
        "C:/Windows/Fonts/Deng.ttf",    // 等线
    };
    ImFont* cjk = nullptr;
    for (const char* path : kCjkFontCandidates) {
        cjk = io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr,
                                           io.Fonts->GetGlyphRangesChineseFull());
        if (cjk) {
            spdlog::info("ImGui CJK font loaded: {}", path);
            break;
        }
    }
    if (!cjk) {
        spdlog::warn("ImGui CJK font unavailable, Chinese text will render as '?'");
    }

    if (!ImGui_ImplSDL2_InitForSDLRenderer(win, ren)) {
        spdlog::error("ImGui: ImGui_ImplSDL2_InitForSDLRenderer failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer2_Init(ren)) {
        spdlog::error("ImGui: ImGui_ImplSDLRenderer2_Init failed");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    spdlog::info("ImGui initialized (backend: SDL2 + SDLRenderer2)");
    return true;
}

void beginImGuiFrame() {
    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();
}

void renderImGui(SDL_Renderer* ren) {
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
}

void processImGuiEvent(const SDL_Event& ev) {
    ImGui_ImplSDL2_ProcessEvent(&ev);
}

void shutdownImGui() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

}  // namespace lw::ui
