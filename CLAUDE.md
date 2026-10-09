# 小电视开发注意事项

## 硬件与构建

- ESP8266，4 MB 闪存，240×240 ST7789，无 PSRAM；USB-C 仅供电。
- 背光 GPIO5 是低电平有效，PWM 0 最亮、1023 关闭。
- 该面板默认需要 `tft.invertDisplay(true)`，设置允许不同面板版本调整。
- TFT 引脚：MOSI=13、SCLK=14、DC=0、RST=2、CS=-1；SPI 40 MHz。
- 默认 CPU 为 160 MHz。
- 当前链接脚本 `eagle.flash.4m1m.ld`：应用上限 1044464 字节；LittleFS 位于 `0x300000`，长度 `0xFA000`。中间区域用于 OTA，不能误称可执行的 3 MB 应用分区。
- 用 `python -m platformio run -e nodemcuv2` 构建固件，`-t buildfs` 构建文件系统。发布包由 `python tools/package.py` 生成。

## 刷写顺序

固件先刷，文件系统后刷。原厂固件依赖自己的字体和配置，先替换文件系统会导致启动失败。刷文件系统会清除 `/config.json`，先通过 `/api/export` 导出，完成后连接 `glimmer-setup`，通过中文页面或 `/api/import` 恢复。

`/update` 的上传字段必须是 `firmware` 或 `filesystem`。合并镜像仅用于 UART，不可上传为 OTA 固件。

## 字体缓存

调用 `tft.loadFont("fonts/<name>", LittleFS)`。不显式传 `LittleFS` 会默认走 SPIFFS 并静默失效。使用 `Display::useFont()` 管理单一缓存；TLS 调用前用 `Display::releaseFont()` 释放缓存以便 BearSSL 分配握手内存。

VLW 格式大端：24 字节头；每个字形 28 字节记录；最后是灰度逐行位图。字体生成器 `tools/genfonts.py` 默认单色栅格化，只保留六个实际使用的字体。

## 页面与画廊

`draw()` 用于激活时的绘制，普通页面可以清屏；`tick()` 每 200 毫秒调用，只更新变化区域。画廊预先合成 RGB565 扫描行并上传，既不直接叠加中间条纹，也不先清屏。使用 `setSwapBytes(true)` 后恢复原状态，确保扫描行字节顺序正确。

画廊种子重建原网络权重，神经元累加使用 double、输出存 Float32，以接近原 JS。不要引入 112 KB 权重矩阵或 115 KB 帧缓冲。保持生成过程中的 `yield()`，并用 `/api/state` 的 `gallery_decode_us` 在实机观察耗时。

`/api/gallery` 流式输出条纹，避免占用大型 JSON 缓冲；网页预览使用这些描述，不自行生成另一个宇宙。

## 设置与运行时

设置持久化必须同时修改存储、API 和网页。普通设置接口对秘密返回 `***`，回传掩码保持已有值，空字符串清除。导出备份故意包含秘密，说明要保管备份。

固定页与轮播都必须在保存后立即生效。至少保留一个可显示页面。时区为带符号分钟，UTC 的 0 是合法值；旧小时字段仅在读取旧备份时迁移。POSIX 时区符号与偏移方向相反，半小时时区也必须保留符号。

Wi-Fi 健康检查同时检查关联状态与非零 IP。失联后原地重置射频并重连，不能仅软件重启，否则可能不能恢复射频状态。没有凭据时保留配网热点。

## 验证边界

主机对照验证的是实际共用的画廊引擎，上游 JS 模块保持不变。浏览器测试模拟 API，只验证网页行为。无物理设备和凭据时不要声称已经刷写、实测 LCD 或在线验证额度。
