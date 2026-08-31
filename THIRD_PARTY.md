# Third-Party Components

The build uses the following components. Versions below are the versions
currently present or pinned by the project build instructions; update this
file whenever a dependency changes.

| Component | Version | Acquisition | License |
|---|---:|---|---|
| Dear ImGui | 1.91.6 | Local `_deps/imgui-src`; CI downloads tag `v1.91.6` | MIT |
| EnTT | 3.16.0 | Local `_deps/entt`; CI downloads tag `v3.16.0` | MIT |
| SDL2 | MSYS2/OS package | `find_package(SDL2)` | zlib |
| SDL2_image | MSYS2/OS package | `find_package(SDL2_image)` | zlib |
| nlohmann/json | MSYS2/OS package | `find_package(nlohmann_json)` | MIT |
| spdlog | MSYS2/OS package | `find_package(spdlog)` | MIT |
| GoogleTest | MSYS2/OS package | `find_package(GTest)` | BSD-3-Clause |

Dear ImGui and EnTT are fetched from their upstream release tags in CI so a
clean Linux build does not depend on an untracked local checkout. The MSYS2
and Linux package versions remain toolchain-managed; record the package
manager output in a release record when producing a binary distribution.

Upstream projects:

- https://github.com/ocornut/imgui/releases/tag/v1.91.6
- https://github.com/skypjack/entt/releases/tag/v3.16.0
- https://github.com/libsdl-org/SDL
- https://github.com/libsdl-org/SDL_image
- https://github.com/nlohmann/json
- https://github.com/gabime/spdlog
- https://github.com/google/googletest
