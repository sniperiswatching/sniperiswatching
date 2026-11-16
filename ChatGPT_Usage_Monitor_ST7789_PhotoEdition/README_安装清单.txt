安装顺序
1. 先按 README.md 的表格接好 ST7789 和 ESP32-S3。
2. 电脑安装 Python 3 和 Arduino IDE。
3. 运行 pc_bridge\start_bridge.bat。
4. Edge 加载 edge_extension 文件夹。
5. 打开 ChatGPT Settings > Usage，并确认 http://127.0.0.1:8765/usage 返回 valid:true。
6. 在 Arduino 固件中修改 Wi-Fi 和 BRIDGE_URL，再上传到 ESP32-S3。
7. 打开 http://127.0.0.1:8765/manage 填写三个任务。
