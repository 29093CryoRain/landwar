// MapPreview.h — 地图缩略预览渲染（开发计划 P6：菜单「选择地图」页）。
// 把已加载 Map 光栅化成一张小纹理：海/陆/山/城用中立色（无首都、无势力色），
// 供 ImGui::Image 直接显示。CPU 光栅（无 SDL_SetRenderTarget 依赖），全在渲染层，不影响模拟确定性。
#pragma once

#include <SDL.h>

namespace lw {
class Map;
}

namespace lw::render {

// 把 map 渲染成地形预览纹理。
//   previewW  — 期望预览宽度（像素）；cell = max(1, round(previewW / map.width))，
//               实际纹理尺寸 = cell×width × cell×height（整数倍，无拉伸）。
//   返回纹理由调用方释放（须先于 SDL_DestroyRenderer）；失败返回 nullptr（spdlog 记错）。
SDL_Texture* renderMapPreview(SDL_Renderer* ren, const Map& map, int previewW);

}  // namespace lw::render
