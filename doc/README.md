# 文档导航

初次 clone 的同事先读 [首次使用指南](getting_started.md)，按目标安装环境并操作。命令默认从仓库根目录执行。

| 文档 | 解决的问题 |
|---|---|
| [根README](../README.md) | 项目是什么、先运行哪个工程、最短演示路径 |
| [PD命名、SDK改名与版本](pd_rename.md) | 新入口、独立配置、固件F4版本及Main UI显示 |
| [首次使用指南](getting_started.md) | clone、工具安装、源码构建、RTL仿真、SOF下载、实板采集、打包、故障定位 |
| [硬件接口和完整引脚](hardware.md) | 器件、28个信号的实际引脚、时钟、复位、ADC通道、电平与SDC |
| [固件架构、寄存器与USB协议](firmware_protocol.md) | 1MSPS采集、上传抽取、寄存器、命令字节序、双组帧、CRC和停止/清空 |
| [固件验证记录](validation.md) | 当前编译/仿真/时序结果、报告路径、实板验证边界 |
| [软件README](../software/README.md) | v1.0.0界面、Get、校准、独立子图、有限/连续、ini恢复 |
| [无硬件演示和SDK调用](../software/docs/simulation.md) | Simulation连接、GUI步骤、公开C++接口及可运行命令行例程 |
| [CSV格式](../software/docs/csv_format.md) | 所选通道、名称、ADC/Power、UTC时间及元数据 |
| [USB驱动安装](../software/docs/USB_DRIVER_INSTALL.txt) | D2XX安装、FT232H枚举和常见连接错误 |
| [软件协议对接](../software/docs/protocol.md) | 四档频率、SDK接收缓存、CRC、通道顺序和生命周期 |
| [软件验证记录](../software/docs/validation.md) | SDK、CSV、ini、离屏界面和安装包的验证范围 |
| [UI性能](../software/docs/performance.md) | 60Hz调度、快照/曲线缓存、性能复现和profile |

`reference_manifest.json`记录固件参考内核，`software/docs/source_manifest.json`记录GUI基线文件，`software/driver/SOURCE.json`记录驱动来源和SHA256。这些是追溯资料；正常使用不需要开发者原电脑的绝对路径或原始参考工程。
