// InputManager.h — SDL 事件 → 输入动作意图（翻新计划 Phase 8，§3.1 app/）。
// 只做事件翻译，不触碰模拟/渲染状态；Application 每帧读取 actions() 并应用。
// 交互不修改 Simulation 内部 → 确定性/回放成立（见翻新计划 §3.7）。
// 键位：空格=暂停/继续；ESC 双击=暂停/继续（非阻塞状态机，单击不动作）；
//       1/2/3/4 = 1x/2x/4x/8x 倍速（玩家操控时 1-6 选兵种）；+/= 快一档、-= 慢一档；
//       F6=步进一帧（跑一个逻辑帧后进入暂停，再按再跑，测试用）；
//       滚轮=缩放（锚定光标）；**右键拖拽=平移**、左键单击=点选（2026-08-03 拖动改右键）；
//       F5=重载配置；F7=重载双色调色板（视觉工程改进 ⑫，不重建模拟）；F12=截图。
// 坐标语义：SDL 在关联了【逻辑尺寸渲染器】的窗口上，已把鼠标【事件】坐标自动换算为
// 逻辑坐标（实测 push 窗口像素 (100,100) → poll 逻辑 (148,148)）。故 actions_.mouseX/Y
// 与 panDX/panDY 都是【逻辑坐标】，Application 直接用（不再 SDL_RenderWindowToLogical）。
// 例外：滚轮缩放锚点用 SDL_GetMouseState（窗口像素），由 Application 自行换算。
// InputManager 不依赖 SDL_Renderer，可独立单测。
#pragma once

#include <SDL.h>

#include <entt/entt.hpp>

namespace lw::app {

// 点选结果（Application 维护，DebugPanel 展示）。交互只读，不修改模拟。
struct Selection {
    enum class Kind { None, Army, Cell };
    Kind kind = Kind::None;
    entt::entity army = entt::null;  // Kind::Army：选中的兵实体
    int cellIndex = -1;              // Kind::Cell：选中的密铺格下标（唯一身份）
    int cellX = 0, cellY = 0;        // Kind::Cell：选中的世界网格坐标（展示用）
};

// 一帧内的输入动作意图（Application 消费并应用）。
struct InputActions {
    bool togglePause = false;     // 空格边沿 或 ESC 双击 → 切换暂停
    int speedSelect = 0;          // 0=无；数字键直接设定档位（1/2/4/8）
    int speedCycle = 0;           // +/-：+1 升一档、-1 降一档
    double wheelDelta = 0.0;      // 本帧滚轮档数累积（+滚入，-滚出）
    bool reloadConfig = false;    // F5：重载配置（重建模拟）
    bool reloadPalette = false;   // F7：重载双色调色板（不重建模拟，视觉工程改进 ⑫）
    bool screenshot = false;      // F12：截图
    bool stepFrame = false;       // F6：步进一帧（跑一个逻辑帧后暂停）
    int selectUnitType = -1;      // 数字键 1-6 → 兵种（ArmyType 0..5）；-1=无（P2）。
                                  // 与 speedSelect（1-4）同源；Application 按玩家模式择一应用。
    bool selectNone = false;      // ` 键 → 不生产（selectedType=-1，2026-08-03）。
    int mouseX = 0, mouseY = 0;   // 鼠标最近已知位置（逻辑坐标，SDL 已自动换算）
    bool dragActive = false;      // 右键拖拽中（平移；2026-08-03 交互调整）
    double panDX = 0.0, panDY = 0.0;  // 本帧拖拽平移增量（逻辑坐标）
    bool click = false;           // 本帧完成一次无拖拽左键单击（点选）
};

class InputManager {
public:
    // 每帧事件循环前调用：重置帧内累积动作（按键边沿/拖拽增量按帧消费）。
    // 鼠标位置**保留**（最近一次已知位置）：无 motion 事件的帧 mouseX/Y 不归零，
    // 否则玩家悬停指示会随"有/无 motion 事件"闪烁（P2 修正）。
    void beginFrame() {
        const int mx = actions_.mouseX, my = actions_.mouseY;
        actions_ = InputActions{};
        actions_.mouseX = mx;
        actions_.mouseY = my;
    }

    // 处理单个 SDL 事件（翻译进 actions_）。
    void handleEvent(const SDL_Event& ev);

    // 本帧动作（事件循环后由 Application 读取）。
    const InputActions& actions() const { return actions_; }

    // 鼠标是否落在 ImGui 窗口上（Application 每帧从 io.WantCaptureMouse 设置）。
    // 该状态下的拖拽/点选/滚轮属于 UI，不作用于游戏地图。
    void setMouseCaptured(bool c) { mouseCaptured_ = c; }

private:
    void onKeyDown(const SDL_Event& ev);
    void onMouseMotion(const SDL_Event& ev);
    void onMouseButton(const SDL_Event& ev);
    void onWheel(const SDL_Event& ev);

    InputActions actions_;
    bool mouseCaptured_ = false;
    // 左键：仅判定"拖拽"以抑制单击（**不再平移**）；单击 = 点选/城市选择。
    bool mouseDown_ = false;
    bool dragging_ = false;
    int downX_ = 0, downY_ = 0;
    // 右键：**拖动 = 平移**（2026-08-03 交互调整，拖动屏幕改右键）。
    bool rightMouseDown_ = false;
    bool rightDragging_ = false;
    int rightDownX_ = 0, rightDownY_ = 0;
    Uint32 lastEscTime_ = 0;    // 上次 ESC 按下时间（双击状态机）
    bool lastEscPending_ = false;  // 是否在等待第二次 ESC
};

}  // namespace lw::app
