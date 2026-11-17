# ESP32-S3 + 2 英寸 ST7789V2 4-SPI ChatGPT 额度监控

这个版本把屏幕改为 2 英寸 ST7789V2 240×320 TFT 彩屏，使用 4-SPI 接口，并按照你之前提供的彩屏图片布局。

屏幕包含：

- ChatGPT / Work / Codex 标题
- 5 小时限额
- 每周限额
- 剩余百分比和进度条
- 最近三个项目
- 项目完成、进行中、待授权、出错状态
- Wi-Fi 和电脑桥接状态
- 重置时间

## 目录结构

    ChatGPT_Usage_Monitor_ST7789V2_2in/
    ├─ ChatGPT_Usage_Monitor/
    │  └─ ChatGPT_Usage_Monitor.ino
    ├─ pc_bridge/
    │  ├─ bridge.py
    │  ├─ tasks.json
    │  ├─ start_bridge.bat
    │  ├─ stop_bridge.bat
    │  ├─ install_autostart.bat
    │  ├─ install_autostart.ps1
    │  ├─ uninstall_autostart.bat
    │  └─ uninstall_autostart.ps1
    ├─ edge_extension/
    │  ├─ manifest.json
    │  ├─ content.js
    │  └─ background.js
    └─ docs/
       ├─ effect_mockup.svg
       └─ wiring_diagram.svg

## 一、ST7789V2 4-SPI 接线

请按照屏幕模块右侧从上到下的针脚连接。

| TFT 屏幕引脚 | ESP32-S3 N16R8 | 作用 |
|---|---:|---|
| GND | GND | 电源地 |
| VCC | 3V3 | 3.3 V 供电 |
| SCL | GPIO12 | SPI SCK 时钟 |
| SDA | GPIO11 | SPI MOSI 数据 |
| RES | GPIO14 | 屏幕复位 |
| DC | GPIO13 | 数据 / 命令选择 |
| CS | GPIO10 | SPI 片选 |
| BLK | 按模块规格连接 | 背光供电或使能 |

照片中的 SCL 和 SDA 在这块 ST7789V2 屏幕上不是 I²C 信号。

- SCL 对应 SPI SCK。
- SDA 对应 SPI MOSI。
- 4-SPI 不需要 MISO。
- 屏幕没有触摸控制时，不需要额外触摸引脚。

### BLK 背光

BLK 必须按照屏幕卖家规格连接。常见模块可以接 3V3，也有模块需要通过背光使能电路控制。固件没有占用 GPIO 驱动 BLK。

## 二、Arduino 工程修改位置

打开：

    ChatGPT_Usage_Monitor/ChatGPT_Usage_Monitor.ino

修改顶部三项：

    const char *WIFI_SSID = "你的 Wi-Fi 名称";
    const char *WIFI_PASSWORD = "你的 Wi-Fi 密码";
    const char *BRIDGE_URL = "http://192.168.8.251:8765/usage";

程序中的屏幕引脚已经按照片设置：

    TFT_SCLK = GPIO12
    TFT_MOSI = GPIO11
    TFT_CS   = GPIO10
    TFT_DC   = GPIO13
    TFT_RST  = GPIO14

## 三、Arduino IDE 安装

1. 安装 Arduino IDE。
2. 打开开发板管理器。
3. 安装 esp32 by Espressif Systems。
4. 选择 ESP32S3 Dev Module。
5. 在库管理器安装：
   - Adafruit GFX Library
   - Adafruit ST7735 and ST7789 Library
   - Adafruit BusIO
   - ArduinoJson 7.x
   - U8g2_for_Adafruit_GFX
6. 打开 ChatGPT_Usage_Monitor.ino。
7. 点击“验证”。
8. 选择 ESP32-S3 的 COM 端口。
9. 点击“上传”。

代码使用 Adafruit_ST7789 驱动，适用于常见 ST7789V2 240×320 模块。

## 四、屏幕显示逻辑

屏幕分为以下区域：

- 顶部显示 ChatGPT 和 Work / Codex 额度监控。
- 左侧卡片显示 5 小时限额。
- 右侧卡片显示每周限额。
- 下方显示最近三个项目。
- 每个项目行显示项目名称、项目说明和状态。
- 底部显示数据是否同步。

项目颜色：

| 项目状态 | 颜色 |
|---|---|
| completed | 绿色 |
| working | 橙色 |
| attention | 橙色，表示需要手动授权 |
| error | 红色 |

项目内容来自：

    pc_bridge/tasks.json

也可以打开电脑端管理页面修改：

    http://127.0.0.1:8765/manage

修改后，ESP32 下一次轮询会刷新项目列表。

## 五、电脑端桥接服务

电脑和 ESP32 必须连接同一个 Wi-Fi。

### 第一次安装自动启动

进入：

    pc_bridge

双击：

    install_autostart.bat

以后每次 Windows 用户登录后，bridge.py 会自动在后台启动。

### 手动检查服务

浏览器打开：

    http://127.0.0.1:8765/health

正常结果：

    {"ok": true, "service": "chatgpt-usage-bridge"}

检查数据：

    http://127.0.0.1:8765/usage

看到 valid 为 true，说明额度数据已经进入桥接服务。

### 取消自动启动

双击：

    uninstall_autostart.bat

### 手动启动和停止

启动：

    start_bridge.bat

停止：

    stop_bridge.bat

## 六、Edge 扩展数据同步

1. Edge 打开 edge://extensions。
2. 开启“开发人员模式”。
3. 点击“加载解压缩的扩展”。
4. 选择 edge_extension 文件夹。
5. 在已登录 ChatGPT 的 Edge 中打开 Settings → Usage。
6. 保持 Usage 页面打开。
7. 扩展会读取页面上显示的 5 小时限额和每周限额。
8. 扩展大约每 30 秒向电脑 bridge.py 发送一次数据。
9. ESP32 每 15 秒从 bridge.py 请求一次数据。

## 七、完整数据流程

    ChatGPT Usage 页面
          ↓
    Edge 扩展读取可见额度
          ↓
    bridge.py 接收并保存额度
          ↓
    ESP32-S3 通过 Wi-Fi 访问 /usage
          ↓
    ST7789V2 彩屏显示额度和三个项目

## 八、黑屏和方向错误处理

### 黑屏

依次检查：

1. VCC 是否接 3V3。
2. GND 是否可靠连接。
3. BLK 是否按照模块规格供电。
4. SCL 是否接 GPIO12。
5. SDA 是否接 GPIO11。
6. RES、DC、CS 是否接对。

### 画面方向错误

修改：

    #define TFT_ROTATION 0

可尝试：

    0、1、2、3

### 颜色反相

修改：

    #define TFT_INVERT false

如果屏幕颜色反相，改成：

    #define TFT_INVERT true

### 屏幕尺寸不同

本版本默认：

    TFT_WIDTH  = 240
    TFT_HEIGHT = 320

如果你的模块是横屏 320×240，可以把旋转方向改为 1 或 3。

## 九、与 0.96 英寸 OLED 版本的区别

本版本不再使用 OLED 的 I²C 接口。

- 屏幕驱动改为 ST7789V2。
- 通信方式改为 4-SPI。
- SCL/SDA 改为 SPI SCK/MOSI。
- 增加 RES、DC、CS。
- 屏幕可以直接显示绿色、橙色和红色项目方框。
- 三个项目可以同时显示在一块彩屏上。

