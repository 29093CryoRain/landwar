// Log.h — spdlog 全局初始化（模式与 envcheck 一致）。
#pragma once

#include <spdlog/spdlog.h>

namespace lw {

inline void initLog() {
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
}

}  // namespace lw
