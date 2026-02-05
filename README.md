# AC IR Remote

本项目包含两部分：
- `AC_IR_Remote.ino`：ESP32-S3 固件（IR 空调控制 / IR 学习 / 显示 / 传感器 / MQTT / Firebase）
- `cloudflare_dashboard/`：Cloudflare Worker + 静态仪表盘（下发命令、查看状态/历史）
- 流程说明文档：`PROJECT_FLOW.md`

## Arduino（ESP32-S3）
- 打开 `AC_IR_Remote.ino`，选择开发板 `ESP32S3 Dev Module`，上传
- 串口监视器：`115200`
- 常用配置：`config.h`（功能/引脚/阈值）、`secrets.h`（凭据）

## Dashboard（Cloudflare）
- 进入 `cloudflare_dashboard/`
- 本地：`npx wrangler dev`
- 部署：`npx wrangler deploy`
- Worker 环境变量：`FIREBASE_DB_URL`、`FIREBASE_AUTH`、`FIREBASE_DEVICE_ID`、`FIREBASE_STATUS_PATH`、`FIREBASE_HISTORY_PATH`、`FIREBASE_ALARMS_PATH`、`FIREBASE_HISTORY_ORDER_BY`

## MQTT
- 默认 Topic：发布状态 `ac/status`，订阅命令 `ac/command`（见 `config.h` / `cloudflare_dashboard/public/app.js`）
