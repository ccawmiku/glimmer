# 项目入口

本仓库是 GeekMagic SmallTV-Ultra 的 ESP8266 精简固件，只有时间与额度、反重力额度、设备状态、潜在空间画廊四页。网页在 `data/web/`，使用原生 JavaScript，必须保持中文。

非平凡修改前阅读 `CLAUDE.md`；刷写步骤见 `FLASHING.md`，画廊评估见 `docs/GALLERY_PORT.md`。

## 必须遵守

1. 页面 `tick()` 不调用 `Display::clear()` 或 `tft.fillScreen()`。普通页面按区域更新；画廊先合成一行，再一次上传。
2. 字体通过 `Display::useFont()` 加载，路径为 `fonts/<name>`，`loadFont` 必须显式传 `LittleFS`。
3. 新设置同时维护 `storage.h`、`storage.cpp`、`web.cpp`、`data/web/index.html`，并按需要维护 `data/web/js/app.js` 的表单逻辑。
4. 文件系统刷写会清除配置，必须先导出备份；固件先刷，文件系统后刷。
5. 不分配完整权重矩阵或全屏缓冲。TLS 调用前释放字体缓存，保留 `yield()`。
6. 不重新加入天气、推送、MCP、吉祥物、动画转场等已移除功能，除非用户明确要求。
7. 交付时更新版本，构建固件和文件系统，并执行 `python tools/package.py` 生成镜像包。

## 目录

- `src/main.cpp`：四页注册、轮播、配网、刷新。
- `src/core/`：屏幕、配置、存储、中文网页 API。
- `src/data/`：反重力和 Codex 额度客户端。
- `src/channels/`：四个页面。
- `src/gallery/`：可在 ESP 与主机共用的低内存随机网络和条纹引擎。
- `data/`：六个使用中的字体和中文网页。
- `tests/`：上游画廊参照与浏览器验证，不能放入设备文件系统。
- `tools/package.py`：分区镜像、串口合并镜像、中文说明、校验值。

## 验证

```bash
python tests/verify_gallery.py
npm ci
npx playwright install chromium
npm run test:web
python -m platformio run -e nodemcuv2
python -m platformio run -e nodemcuv2 -t buildfs
python tools/package.py
```

未接实机时明确说明验证边界，不能把模拟 API 或主机图片说成设备实测。
