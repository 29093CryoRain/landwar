# Asset and Dependency Notices

This file records the current release audit boundary. It is intentionally
separate from `LICENSE`: the MIT license for the source code does not grant
rights to third-party assets or libraries.

## Project Assets

| Asset | Current status |
|---|---|
| `data/map_*.bmp` | Hand-drawn project maps. The project author confirmed that these maps may be redistributed with the project. |
| `data/army*.png`, `data/tower/*.png`, `data/mountain.png`, `data/city.png`, `data/ring.png`, `data/arrow*.png` | Attribution or author permission is not recorded in this repository yet. Do not mark a public release complete until this row is confirmed. |
| CJK fonts | Not bundled. Windows system fonts are probed at runtime; users must obtain and license those fonts independently. |

## Review Rule

Before publishing a release archive, add the author/source and license for
each asset row that is not already cleared. Do not copy a font or an asset
into the archive merely to make the package self-contained.

Third-party code and its licenses are listed in `THIRD_PARTY.md`.
