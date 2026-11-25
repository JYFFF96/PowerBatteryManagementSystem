# Power Battery Management System

动力电池及 BMS 管理系统调试软件（Qt 6 + C++17 + Qt Widgets）。

## 功能（v0.1 模拟演示版）

- 实时电池总览：SOC、SOH、电压、电流、功率、最高温度、单体压差
- 24 节单体电压表、6 路温度表
- 故障状态提示与事件日志
- 可启动/暂停的模拟 BMS 数据（每秒刷新）
- 导出当前单体电压 CSV
- 初始为模拟模式；**未连接真实 BMS 硬件**

> 当前读数和阈值仅作演示，不能作为真实设备的安全保护策略。
> 真实 CAN/串口协议、报警阈值和设备参数必须根据对应 BMS 厂家的文档实现与验证。

## 环境

- Qt 6.2 或更高版本，包含 Qt Widgets
- CMake 3.21+
- 支持 MSVC 2022 / MinGW / Linux Qt 6 工具链

## 构建

用 Qt Creator 打开根目录的 `CMakeLists.txt`，选择 Qt 6 Kit，点击构建并运行。

或使用终端：

```bash
cmake -S . -B build
cmake --build build --config Release
```

如 CMake 找不到 Qt，添加 `-DCMAKE_PREFIX_PATH=<Qt安装目录>/<版本>/<Kit>`。

## 下一阶段

1. 根据参考页面截图完善完整 UI。
2. 抽象 BMS 数据模型、通讯驱动和协议解析。
3. 依据真实设备协议接入 CAN/串口。
4. 增加趋势曲线、历史存储、故障码解析、参数读写。

仓库名中的前导连字符 `-PowerBatteryManagementSystem` 是 GitHub 仓库本身的名称；可执行程序名称不带前导连字符。
