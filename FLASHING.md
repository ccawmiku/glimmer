# 中文刷写说明

目标设备：GeekMagic SmallTV-Ultra，ESP8266，4 MB 闪存，240×240 ST7789。其他型号不能据此保证兼容。设备自带 USB-C 接口只有供电，没有串口数据功能。

## 文件选择

| 文件 | 用途 | 地址或上传字段 |
|---|---|---|
| firmware.bin | 固件 | 网页选择“固件文件”，或表单字段 `firmware`；串口地址 `0x0` |
| littlefs.bin | 字体和中文网页 | 网页选择“文件系统文件”，或字段 `filesystem`；串口地址 `0x300000` |
| flash-all.bin | 串口合并镜像 | 仅串口，地址 `0x0`；不要通过网页上传 |

文件系统镜像长度为 `0xFA000` 字节，对应 `0x300000` 到 `0x3FA000`，不覆盖末尾校准区域。包内 `SHA256SUMS` 可用于核对下载文件。

## 无线刷写：先固件，后文件系统

如果是原厂固件，先通过原厂配网页把设备连接到家里的 2.4 GHz 无线网络，再查出设备 IP。原厂热点常见名称为 `GIFTV`，配网页地址为 `192.168.4.1`。

1. 已安装 glimmer 的设备，先打开中文面板“备份与刷写”导出设置。也可执行下方备份命令。
2. 在设备的升级入口上传 `firmware.bin`。**必须先刷固件**；原厂固件依赖自己的文件系统，先替换文件系统可能导致启动失败。
3. 等设备完成重启。首次安装可能进入 `glimmer-setup` 热点；连接该热点后用 `192.168.4.1` 作为下一步地址。
4. 上传 `littlefs.bin`。更新文件系统会清除保存的网络密码和额度凭据。
5. 设备重启后，连接 `glimmer-setup`，打开 `http://192.168.4.1/`。
6. 在“备份与刷写”恢复原 glimmer 设置备份，或者在“网络与额度”填写网络信息并“保存并重启”。
7. 设备回到家庭网络后，打开其 IP 或 `http://glimmer.local/`。

命令行等价操作如下，在解压目录内执行。把地址替换为设备实际地址；每次上传后等待设备重启，再继续下一步。

```bash
# 已安装 glimmer 时先备份；原厂固件不适用此备份接口。
curl --fail http://192.168.1.88/api/export -o glimmer-config-backup.json

# 第一步：固件。
curl --fail -F 'firmware=@firmware.bin' http://192.168.1.88/update

# 第二步：文件系统。若设备已进入配网热点，地址改为 192.168.4.1。
curl --fail -F 'filesystem=@littlefs.bin' http://192.168.1.88/update

# 文件系统更新后：连接 glimmer-setup，再恢复备份。
curl --fail -H 'Content-Type: application/json' --data-binary @glimmer-config-backup.json http://192.168.4.1/api/import
```

备份包含网络密码和额度凭据，应保存在自己的设备上。已有 glimmer 的日后更新，若字体和网页没有变化，可以只刷固件保留设置；**本次中文网页更新需要刷两个文件**。

## 串口刷写或恢复

需要 USB-TTL 适配器和设备 UART 接线；设备自带的 USB-C 电源接口不能用于此操作。串口信号使用 3.3 V 电平，TX 接设备 RX，RX 接设备 TX，GND 共地，上电时 GPIO0 接地进入下载模式。

安装工具：

```bash
python -m pip install esptool
```

如果安装的是新版 esptool，Windows 串口例如 `COM5`，Linux 例如 `/dev/ttyUSB0`，macOS 例如 `/dev/cu.usbserial-0001`。任选一种写法：

```bash
# 分别写两个镜像。
python -m esptool --chip esp8266 --port COM5 --baud 460800 write-flash --flash-size 4MB 0x0 firmware.bin 0x300000 littlefs.bin

# 或一次写入串口合并镜像。
python -m esptool --chip esp8266 --port COM5 --baud 460800 write-flash --flash-size 4MB 0x0 flash-all.bin
```

旧版 esptool 的子命令为 `write_flash`，参数为 `--flash_size`。刷写结束后断开 GPIO0 与地的连接，重新上电。

## 首次配置

中文面板“屏幕设置”中选择四页。关闭“自动轮播”后，可固定显示“潜在空间画廊”。画廊只需设置“画面刷新间隔”，每次刷新重新生成整个随机宇宙。页面轮播间隔与画廊画面刷新间隔独立。

“网络与额度”里填写反重力或 Codex 的现有凭据。留空保存会清除对应凭据；显示 `***` 表示已有凭据保持不变。无额度凭据时，时间页仍可显示时间，反重力页提示尚未配置。
