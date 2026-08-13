// tools/measure_visual_radius.cpp — 量取 army.png 各兵种的视觉半径（Phase 8 点选用）。
// 方案决策：视觉半径外置到 data/config.json 的 units[].visualRadius（数据驱动，与项目
// 魔法数字外置原则一致）；本工具是"开发时算一次"的程序化手段，保证量法可复现。
// 用法（在 new_project_landwar/ 下）：
//   g++ -o /tmp/mvr tools/measure_visual_radius.cpp -I$(MSYS2)/include/SDL2 -I$(MSYS2)/include \
//       -L$(MSYS2)/lib -lSDL2 -lSDL2_image && /tmp/mvr data/army.png
// 量法：每个兵种（行 t）取各势力（列 f）的 32×32 子图，可见像素(alpha>16)到
// 子图中心(16,16)的最大距离 × 绘制缩放(48/32=1.5) = 视觉半径（逻辑像素, zoom=1）。
// 程序量得（2026-08）：普通12 / 先锋13 / 开拓16 / 激光25 / 爆炸22 / 地雷17（最远像素距离）。
// 用户目测校准（略小于最远值）：普通12 / 先锋13 / 开拓15 / 激光20 / 爆炸21 / 地雷17 —— 以
// 此为准写入 config.json。更新 army.png 后重跑本工具作参考，再按目测微调写回 config.json。
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

// 从 army.png 量出每个兵种(行)的视觉半径。
// 单元：32x32 子图；绘制缩放到 48x48（×1.5）。锚点 = 子图中心 (16,16)（绘制中心）。
// 半径 = 可见像素(alpha>th)到锚点的最大距离 × 1.5（逻辑像素, zoom=1 空间）。
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "data/army.png";
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface* surf = IMG_Load(path);
    if (!surf) { printf("load failed: %s\n", IMG_GetError()); return 1; }
    printf("image %dx%d\n", surf->w, surf->h);
    const double scale = 48.0 / 32.0;  // 32 -> 48
    for (int t = 0; t < 8; ++t) {  // 行 = 兵种
        double maxPerFaction[8] = {0};
        // 每列 = 势力(0..7)，量每格后取跨势力最大
        for (int f = 0; f < 8; ++f) {
            double maxR = 0;
            for (int py = 0; py < 32; ++py) {
                for (int px = 0; px < 32; ++px) {
                    int sx = f*32 + px, sy = t*32 + py;
                    Uint8 r,g,b,a; Uint32 p;
                    Uint8* q = (Uint8*)surf->pixels + sy*surf->pitch + sx*4;
                    a = q[3];
                    if (a > 16) {
                        double dx = px - 15.5, dy = py - 15.5;  // 锚点=格中心(16,16)的连续坐标
                        double rr = std::sqrt(dx*dx + dy*dy) * scale;
                        if (rr > maxR) maxR = rr;
                    }
                }
            }
            maxPerFaction[f] = maxR;
        }
        double maxAll = *std::max_element(maxPerFaction, maxPerFaction+8);
        double avg = 0; for (double m : maxPerFaction) avg += m; avg /= 8;
        printf("type %d: 跨势力max=%.1fpx  平均=%.1fpx  perFaction=[", t, maxAll, avg);
        for (int f = 0; f < 8; ++f) printf("%.0f%s", maxPerFaction[f], f<7?",":"");
        printf("]\n");
    }
    SDL_FreeSurface(surf); SDL_Quit();
    return 0;
}
