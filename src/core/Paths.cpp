// Paths.cpp — 目录创建实现（Windows CreateDirectoryA；非 Windows 用 mkdir 兜底）。
// 说明：core 库整体保持"纯逻辑"，本文件是唯一含平台调用的例外——
// 目录创建无处安放（MapGenerator 生成地图、Application 保存 options/截图都在运行期写盘），
// 归入 core 便于三者共用；仅依赖 kernel32（自动链接），不影响无头/测试。
#include "core/Paths.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace lw {

bool ensureDirExists(const std::string& path) {
    if (path.empty()) return true;
#ifdef _WIN32
    // CreateDirectoryA 一次只建一层 → 沿分隔符逐级尝试。
    std::string cur;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        cur += c;
        if (c == '/' || c == '\\') {
            // 跳过前导分隔符（如 "/a"）与连续分隔符；最后一个分隔符后的段留到下一轮。
            continue;
        }
        // 当前字符是某段末尾（下一字符是分隔符或串尾）→ 尝试创建该段。
        const bool segmentEnd = (i + 1 >= path.size()) || (path[i + 1] == '/' || path[i + 1] == '\\');
        if (!segmentEnd) continue;
        // "C:" 盘符前缀：段仅由字母+冒号组成 → 不是可创建路径，跳过（"C:\..." 的 "C:" 段）。
        if (cur.size() == 2 && cur[1] == ':') continue;
        if (CreateDirectoryA(cur.c_str(), nullptr) == 0) {
            const DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) return false;  // 其余错误视为失败
        }
    }
    return true;
#else
    // POSIX：mkdir 逐级（mode 0755）。
    std::string cur;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        cur += c;
        if (c != '/' && !(i + 1 >= path.size())) continue;
        if (cur == "." || cur == "/" || cur == "..") continue;
        if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
#endif
}

bool ensureUserDataDirs() {
    if (!ensureDirExists(kUserDataDir)) return false;
    if (!ensureDirExists(kGeneratedMapDir)) return false;
    return ensureDirExists(kScreenshotDir);
}

}  // namespace lw
