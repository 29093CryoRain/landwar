// InputManager.cpp — SDL 事件翻译实现（翻新计划 Phase 8）。
#include "app/InputManager.h"

#include <cmath>

#include "core/GameDefs.h"  // ArmyType（数字键 1-6 → 兵种，P2）

namespace lw::app {

namespace {
constexpr int kDragThresholdPx = 6;            // 按下后移动超过此像素 → 判定拖拽（否则单击）
}  // namespace

void InputManager::handleEvent(const SDL_Event& ev) {
    switch (ev.type) {
        case SDL_KEYDOWN:
            onKeyDown(ev);
            break;
        case SDL_MOUSEMOTION:
            onMouseMotion(ev);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            onMouseButton(ev);
            break;
        case SDL_MOUSEWHEEL:
            onWheel(ev);
            break;
        default:
            break;
    }
}

void InputManager::onKeyDown(const SDL_Event& ev) {
    switch (ev.key.keysym.sym) {
        case SDLK_SPACE:
            actions_.togglePause = true;
            break;
        // 数字键 1-6：玩家模式选兵种（P2）；1-4 同时保留倍速意图（speedSelect），
        // Application 按是否玩家操控择一应用（玩家操控时 1-6 选兵种，倍速走 +/- 与面板）。
        case SDLK_1:
            actions_.speedSelect = 1;
            actions_.selectUnitType = static_cast<int>(ArmyType::normal);
            break;
        case SDLK_2:
            actions_.speedSelect = 2;
            actions_.selectUnitType = static_cast<int>(ArmyType::vanguard);
            break;
        case SDLK_3:
            actions_.speedSelect = 4;
            actions_.selectUnitType = static_cast<int>(ArmyType::pioneer);
            break;
        case SDLK_4:
            actions_.speedSelect = 8;
            actions_.selectUnitType = static_cast<int>(ArmyType::laser);
            break;
        case SDLK_5:
            actions_.selectUnitType = static_cast<int>(ArmyType::bomb);
            break;
        case SDLK_6:
            actions_.selectUnitType = static_cast<int>(ArmyType::mine);
            break;
        // P9：数字键 7/8 选手枪/霰弹（仅选兵种，不触发倍速意图）。
        case SDLK_7:
            actions_.selectUnitType = static_cast<int>(ArmyType::pistol);
            break;
        case SDLK_8:
            actions_.selectUnitType = static_cast<int>(ArmyType::shotgun);
            break;
        case SDLK_BACKQUOTE:  // ` 键 → 不生产（与图标栏最左空框等价，2026-08-03）
            actions_.selectNone = true;
            break;
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:
            actions_.speedCycle = 1;
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            actions_.speedCycle = -1;
            break;
        case SDLK_F5:
            actions_.reloadConfig = true;
            break;
        case SDLK_F7:
            actions_.reloadPalette = true;  // 重载双色调色板（F6 已用于步进）
            break;
        case SDLK_F12:
            actions_.screenshot = true;
            break;
        case SDLK_F6:
            actions_.stepFrame = true;  // 步进一帧（跑一个逻辑帧后进入暂停）
            break;
        case SDLK_ESCAPE: {
            actions_.escape = true;
            break;
        }
        default:
            break;
    }
}

void InputManager::onMouseMotion(const SDL_Event& ev) {
    actions_.mouseX = ev.motion.x;
    actions_.mouseY = ev.motion.y;
    if (mouseCaptured_ || (!mouseDown_ && !rightMouseDown_)) return;
    // **右键拖动 = 平移**（2026-08-03 交互调整）：按下后移动超过阈值才判定拖拽。
    if (rightMouseDown_ && (ev.motion.state & SDL_BUTTON_RMASK)) {
        if (!rightDragging_ && std::hypot(ev.motion.x - rightDownX_, ev.motion.y - rightDownY_) >=
                                   kDragThresholdPx) {
            rightDragging_ = true;
        }
        if (rightDragging_) {
            actions_.dragActive = true;
            actions_.panDX += ev.motion.xrel;
            actions_.panDY += ev.motion.yrel;
        }
    }
    // 左键：仅判定拖拽（用于抑制单击），不再平移。
    if (mouseDown_ && (ev.motion.state & SDL_BUTTON_LMASK)) {
        if (!dragging_ && std::hypot(ev.motion.x - downX_, ev.motion.y - downY_) >=
                              kDragThresholdPx) {
            dragging_ = true;
        }
    }
}

void InputManager::onMouseButton(const SDL_Event& ev) {
    actions_.mouseX = ev.button.x;
    actions_.mouseY = ev.button.y;
    if (ev.button.button == SDL_BUTTON_LEFT) {
        // 左键：单击点选/城市选择；拖拽仅抑制单击。
        if (ev.type == SDL_MOUSEBUTTONDOWN) {
            if (mouseCaptured_) return;  // 按下落在 ImGui 上：不参与游戏点选
            mouseDown_ = true;
            dragging_ = false;
            downX_ = ev.button.x;
            downY_ = ev.button.y;
        } else {  // MOUSEBUTTONUP
            if (mouseCaptured_) {  // 松开落在 ImGui 上：不算单击
                mouseDown_ = false;
                dragging_ = false;
                return;
            }
            if (!mouseDown_) return;
            mouseDown_ = false;
            if (!dragging_) actions_.click = true;  // 无拖拽 → 单击点选
            dragging_ = false;
        }
    } else if (ev.button.button == SDL_BUTTON_RIGHT) {
        // 右键：拖动平移（松开不产生点击）。
        if (ev.type == SDL_MOUSEBUTTONDOWN) {
            if (mouseCaptured_) return;  // 按下落在 ImGui 上：不参与游戏拖拽
            rightMouseDown_ = true;
            rightDragging_ = false;
            rightDownX_ = ev.button.x;
            rightDownY_ = ev.button.y;
        } else {  // MOUSEBUTTONUP
            rightMouseDown_ = false;
            rightDragging_ = false;
        }
    }
}

void InputManager::onWheel(const SDL_Event& ev) {
    if (mouseCaptured_) return;  // 滚轮在 ImGui 窗口上时归 UI
    if (ev.wheel.y != 0) {
        // 只累积缩放档数。滚轮事件不携带位置，缩放锚点由 Application 用
        // SDL_GetMouseState（窗口像素）+ SDL_RenderWindowToLogical 取当前光标位置
        // （否则锚点停在旧值 → 从左上角缩放，用户反馈 Phase 8）。
        actions_.wheelDelta += ev.wheel.y;
    }
}

}  // namespace lw::app
