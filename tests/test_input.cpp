// test_input.cpp — 输入翻译（Phase 8：ESC 退出确认、空格/倍速/截图/重载、
// 鼠标拖拽/单击/滚轮/ImGui 捕获抑制）。InputManager 不依赖渲染器，可纯事件单测。
#include <gtest/gtest.h>

#include <SDL.h>

#include "app/InputManager.h"
#include "app/PausePolicy.h"

namespace {

SDL_Event key(SDL_Keycode sym, Uint32 ts) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = sym;
    e.key.timestamp = ts;
    return e;
}

SDL_Event mouseBtn(Uint32 type, int x, int y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e{};
    e.type = type;
    e.button.button = button;
    e.button.x = x;
    e.button.y = y;
    return e;
}

}  // namespace

using namespace lw::app;

TEST(InputManager, SpaceTogglesPause) {
    InputManager im;
    im.handleEvent(key(SDLK_SPACE, 1000));
    EXPECT_TRUE(im.actions().togglePause);
    EXPECT_FALSE(im.actions().screenshot);
}

TEST(PausePolicy, ResearchSelectionBlocksPauseToggle) {
    EXPECT_FALSE(lw::app::shouldTogglePause(true, true));
    EXPECT_TRUE(lw::app::shouldTogglePause(true, false));
    EXPECT_FALSE(lw::app::shouldTogglePause(false, false));
}

TEST(PausePolicy, EscapeTransitionsConfirmationState) {
    EXPECT_EQ(lw::app::escapeAction(false, false), lw::app::EscapeAction::None);
    EXPECT_EQ(lw::app::escapeAction(true, false), lw::app::EscapeAction::OpenConfirmation);
    EXPECT_EQ(lw::app::escapeAction(true, true), lw::app::EscapeAction::ResumeGame);
}

TEST(InputManager, EscRequestsExitConfirmation) {
    InputManager im;
    im.handleEvent(key(SDLK_ESCAPE, 1000));
    EXPECT_TRUE(im.actions().escape);
    EXPECT_FALSE(im.actions().togglePause);
}

TEST(InputManager, EscIsAnIndependentActionEachPress) {
    InputManager im;
    im.handleEvent(key(SDLK_ESCAPE, 1000));
    EXPECT_TRUE(im.actions().escape);
    im.beginFrame();
    im.handleEvent(key(SDLK_ESCAPE, 1300));
    EXPECT_TRUE(im.actions().escape);
    EXPECT_FALSE(im.actions().togglePause);
}

TEST(InputManager, EscTimestampDoesNotChangeAction) {
    InputManager im;
    im.handleEvent(key(SDLK_ESCAPE, 3000));
    EXPECT_TRUE(im.actions().escape);
}

TEST(InputManager, SpeedKeys) {
    InputManager im;
    im.handleEvent(key(SDLK_3, 1));
    EXPECT_EQ(im.actions().speedSelect, 4);
    im.handleEvent(key(SDLK_2, 2));
    EXPECT_EQ(im.actions().speedSelect, 2);
    im.handleEvent(key(SDLK_EQUALS, 3));
    EXPECT_EQ(im.actions().speedCycle, 1);
    im.handleEvent(key(SDLK_MINUS, 4));
    EXPECT_EQ(im.actions().speedCycle, -1);
}

TEST(InputManager, ScreenshotAndReloadKeys) {
    InputManager im;
    im.handleEvent(key(SDLK_F12, 1));
    EXPECT_TRUE(im.actions().screenshot);
    im.handleEvent(key(SDLK_F5, 2));
    EXPECT_TRUE(im.actions().reloadConfig);
    im.handleEvent(key(SDLK_F6, 3));  // 步进一帧（不改变其他意图）
    EXPECT_TRUE(im.actions().stepFrame);
    EXPECT_TRUE(im.actions().screenshot);
    EXPECT_TRUE(im.actions().reloadConfig);
}

TEST(InputManager, StepFrameKeyDoesNotTouchOtherIntents) {
    InputManager im;
    im.handleEvent(key(SDLK_F6, 1));
    EXPECT_TRUE(im.actions().stepFrame);
    im.beginFrame();
    EXPECT_FALSE(im.actions().stepFrame);
    EXPECT_FALSE(im.actions().screenshot);
    EXPECT_FALSE(im.actions().reloadConfig);
    EXPECT_FALSE(im.actions().togglePause);
}

TEST(InputManager, ClickSelects) {
    InputManager im;
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONDOWN, 100, 100));
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONUP, 101, 100));
    EXPECT_TRUE(im.actions().click);
    EXPECT_FALSE(im.actions().dragActive);
}

TEST(InputManager, RightDragPansNotClicks) {
    // 2026-08-03 交互调整：拖动屏幕改【右键】。左键拖拽不再平移。
    InputManager im;
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONDOWN, 100, 100, SDL_BUTTON_RIGHT));
    SDL_Event mot{};
    mot.type = SDL_MOUSEMOTION;
    mot.motion.x = 130;
    mot.motion.y = 120;
    mot.motion.xrel = 30;
    mot.motion.yrel = 20;
    mot.motion.state = SDL_BUTTON_RMASK;
    im.handleEvent(mot);
    EXPECT_TRUE(im.actions().dragActive);
    EXPECT_DOUBLE_EQ(im.actions().panDX, 30.0);
    EXPECT_DOUBLE_EQ(im.actions().panDY, 20.0);
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONUP, 130, 120, SDL_BUTTON_RIGHT));
    EXPECT_FALSE(im.actions().click);  // 右键松开不产生单击
    im.beginFrame();                   // 帧末动作清零（dragActive 是帧级标志，由 beginFrame 清）
    EXPECT_FALSE(im.actions().dragActive);
    EXPECT_DOUBLE_EQ(im.actions().panDX, 0.0);
}

TEST(InputManager, LeftDragDoesNotPan) {
    // 左键拖拽不再平移（2026-08-03）。
    InputManager im;
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONDOWN, 100, 100));
    SDL_Event mot{};
    mot.type = SDL_MOUSEMOTION;
    mot.motion.x = 130;
    mot.motion.y = 120;
    mot.motion.xrel = 30;
    mot.motion.yrel = 20;
    mot.motion.state = SDL_BUTTON_LMASK;
    im.handleEvent(mot);
    EXPECT_FALSE(im.actions().dragActive);
    EXPECT_DOUBLE_EQ(im.actions().panDX, 0.0);
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONUP, 130, 120));
    EXPECT_FALSE(im.actions().click);  // 拖拽过 → 不产生单击
}

TEST(InputManager, DragBelowThresholdIsClick) {
    InputManager im;
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONDOWN, 100, 100));
    SDL_Event mot{};
    mot.type = SDL_MOUSEMOTION;
    mot.motion.x = 102;
    mot.motion.y = 101;
    mot.motion.xrel = 2;
    mot.motion.yrel = 1;
    mot.motion.state = SDL_BUTTON_LMASK;
    im.handleEvent(mot);
    EXPECT_FALSE(im.actions().dragActive);
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONUP, 102, 101));
    EXPECT_TRUE(im.actions().click);
}

TEST(InputManager, WheelAccumulates) {
    InputManager im;
    SDL_Event w{};
    w.type = SDL_MOUSEWHEEL;
    w.wheel.y = 1;
    im.handleEvent(w);
    w.wheel.y = -1;
    im.handleEvent(w);
    EXPECT_DOUBLE_EQ(im.actions().wheelDelta, 0.0);
    w.wheel.y = 2;
    im.handleEvent(w);
    EXPECT_DOUBLE_EQ(im.actions().wheelDelta, 2.0);
}

TEST(InputManager, MouseCapturedSuppressesMapInput) {
    InputManager im;
    im.setMouseCaptured(true);
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONDOWN, 100, 100));
    im.handleEvent(mouseBtn(SDL_MOUSEBUTTONUP, 101, 100));
    EXPECT_FALSE(im.actions().click);
    EXPECT_FALSE(im.actions().dragActive);
}
