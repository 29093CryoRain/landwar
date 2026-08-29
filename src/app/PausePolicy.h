// PausePolicy.h — 应用层暂停输入策略。
#pragma once

namespace lw::app {

enum class EscapeAction { None, OpenConfirmation, ResumeGame };

constexpr bool shouldTogglePause(bool togglePause, bool researchPending) noexcept {
    return togglePause && !researchPending;
}

constexpr EscapeAction escapeAction(bool escape, bool confirmationOpen) noexcept {
    if (!escape) return EscapeAction::None;
    return confirmationOpen ? EscapeAction::ResumeGame : EscapeAction::OpenConfirmation;
}

}  // namespace lw::app
