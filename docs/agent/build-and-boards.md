# 构建与板型指引

> ESP-IDF 构建、烧录、切换板型、assets 分区。先读 [CLAUDE.md](../../CLAUDE.md)。

## 环境
- ESP-IDF 5.4+（README 约定），装好 ESP-IDF 插件并选 SDK ≥5.4。
- Linux 编译更快且免驱动问题（上游 README 建议）。
- 当前 `sdkconfig` 已配置好 active board，日常无需改。

## 常用命令
```bash
idf.py build                # 编译
idf.py -p /dev/ttyUSB0 flash monitor   # 烧录 + 串口监视
idf.py menuconfig           # 改配置（板型/语言/音频等，见 main/Kconfig.projbuild）
idf.py fullclean && idf.py build        # 干净重建（CMake/源变更后偶尔需要）
```

## 板型选择机制（[main/CMakeLists.txt](../../main/CMakeLists.txt)）
- 一大串 `elseif(CONFIG_BOARD_TYPE_XXX)` 分支（约 70+ 板型，~815 行），每个分支设 `BOARD_TYPE`、`BUILTIN_TEXT_FONT`、`BUILTIN_ICON_FONT`、可选 `DEFAULT_EMOJI_COLLECTION`。
- **active board 由 `sdkconfig` 的 `CONFIG_BOARD_TYPE_*=y` 决定**。本分支 = `CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_EPAPERX`（`sdkconfig.defaults` 未设，实际值在 `sdkconfig`）。
- 切板型：`idf.py menuconfig` → 选 Board Type → 重新 `build`；或直接改 `sdkconfig.defaults.*` 后删 `sdkconfig` 重建。
- 选定后，`file(GLOB boards/${BOARD_TYPE}/*.cc ...)` 把该板型源码纳入编译（[CMakeLists.txt:522](../../main/CMakeLists.txt#L522)）。

## 分区表与 assets
- v2 分区表（与 v1 不兼容，见 [partitions/v2/README.md](../../partitions/v2/README.md)）。
- `assets` 分区存字体/表情/唤醒词模型，由 `scripts/build_default_assets.py` 按 sdkconfig 生成（[CMakeLists.txt:688](../../main/CMakeLists.txt#L688) `build_default_assets_bin`）。
- 三种 assets 模式（[CMakeLists.txt:799](../../main/CMakeLists.txt#L799)）：`FLASH_DEFAULT_ASSETS`（默认生成）/ `FLASH_CUSTOM_ASSETS`（自定义文件/URL）/ `FLASH_NONE_ASSETS`。

## components / managed_components
- `managed_components/`：构建时由 IDF manager 按 `dependencies.lock` 自动拉取，**勿手动改**（`.gitignore` 已忽略）。
- `components/`：本仓库自带组件。`.gitignore` 忽略 `components/*` 但白名单 `components/GxEPD2/`——后者是墨水屏驱动，**含本分支定制的局部刷新 LUT**（清残影），需纳入版本管理。见近期 commit「将GxEPD2库放在工程中」。

## 本分支板型入口
- [compact_wifi_board_epaperx.cc](../../main/boards/bread-compact-wifi-epaperx/compact_wifi_board_epaperx.cc)：`CompactWifiBoardEpaperX`（继承 `WifiBoard`），含 LCD + 墨水屏双显示、`InitializeTools()` 注册 Fridge MCP。
- [config.h](../../main/boards/bread-compact-wifi-epaperx/config.h)：引脚/音频 I2S 定义。

## 验证编译（无硬件也跑）
```bash
idf.py build
```
成功即说明源码/CMake 正确。烧录与运行态验证需实机。
