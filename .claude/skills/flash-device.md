# 刷写设备

使用仓库根目录 `FLASHING.md` 的中文步骤和本次构建文件。必须固件先刷、文件系统后刷，先导出已有 glimmer 配置。不要使用上游 Avinava 的预构建文件替代本仓库中文画廊版。

串口合并镜像仅用于 UART；网页上传使用分别构建的 `firmware.bin` 和 `littlefs.bin`。
