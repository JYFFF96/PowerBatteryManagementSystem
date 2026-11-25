# Power Battery Management System

动力电池及 BMS 调试软件（Qt 6、C++17、Qt Widgets、CMake）。

## 功能

- BMS 模拟演示模式（无硬件时启动）
- 与 [mmWaveRadar](https://github.com/JYFFF96/mmWaveRadar) 相同的 **ControlCAN.dll / VCI_*** 方式连接 Windows USB-CAN 设备
- 设备类型默认 `4`，设备索引可选，CAN0/CAN1 可切换；250 kbps，29 位扩展帧
- TKB BMS V1.1 报文接收和解析：总电压、串数/温感数、SOC、SOH、电流原始值、充放电状态、单体极值、温度极值、故障位状态
- 动态展示最多 192 节单体电压、64 路温度
- 当前单体数据 CSV 导出

## Windows 环境

- Qt 6.2+，Qt Widgets，CMake 3.21+，MSVC 2022 或匹配的 MinGW 工具链
- 安装对应 USB-CAN 设备的驱动
- 从本人的 `mmWaveRadar/libs/ControlCAN.dll` 复制 **ControlCAN.dll 到本程序可执行文件同一目录**（与位数一致）。
  本仓库不再重复提交厂商 DLL。使用你自己的已授权驱动文件。

Qt Creator 打开 `CMakeLists.txt`，选择 Qt 6 Kit，构建运行。

## 连接步骤

1. 用 CAN-H/CAN-L 接入 BMS CAN 总线，确认终端电阻、供电和设备驱动。
2. 选择设备索引（默认 0），CAN0 或 CAN1，点击 **连接 CAN**。
3. 程序采用 `VCI_OpenDevice(4, index, 0)`、`VCI_InitCAN`、`VCI_StartCAN`、`VCI_Receive`。
4. 解析 TKB 扩展帧 `0x02018100`～`0x02058100`、`0x02108100`～`0x023F8100`、`0x02468100`～`0x024D8100`。
5. 需要恢复模拟模式时先点击 **断开 CAN**。

当前代码只读，不向 BMS 发送控制命令。

## 协议与硬件待验证项

- 精简版协议只注明电流分辨率 0.1A，**没有明确 16 位电流有符号编码**；程序显示 `A (raw)`，功率显示“待确认电流符号”，不把错误符号用于工程判断。
- 温度范围标题写 `FID=0x46～0x4D`，表格中的 FID 写 `0x46～0x49`；本实现按标题支持 64 路，需厂家确认。
- `0x02028100` 最严重故障的故障代码表未随精简版提供，当前保存原始代码；故障等级按 `0x02058100` 的 2-bit 字段解码。
- 驱动 DLL 的适配器型号、USB-CAN 硬件和对应驱动版本仍需实机测试。
- 实机报文缺失时 UI 的 `--` 表示未收到；当前没有实现超时标红。
- 模拟模式使用独立的示意数据，不能作为实际 BMS 设备的安全保护逻辑。

## 后续

增加报文日志、超时检测、真实测量趋势曲线与故障码表，并进行 Windows MSVC / USB-CAN 实机验证。
