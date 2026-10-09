# 第三方代码归属

设备项目基于 `Avinava/glimmer`（MIT），保留仓库根目录 `LICENSE`。

`src/gallery/gallery.cpp`、`src/gallery/gallery.h` 的网络、参数映射、色板、稳定采样与条纹算法由 `ccawmiku/latent-art-wallpaper`（版权：王茂，MIT）移植。参照提交：`db934a9d60397db8c0a3bbefb1f007bb3a6616e0`。

完整画廊许可证保存在 `tests/reference/LICENSE`；`tests/reference/` 中的 JavaScript 模块为未修改的上游参照实现，仅用于测试，不随设备文件系统安装。

固件依赖 TFT_eSPI 和 ArduinoJson，按其原许可通过 PlatformIO 使用；保留的字体来自原项目所附的 VT323、Silkscreen 和 DM Mono。
