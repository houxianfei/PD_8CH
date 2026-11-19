# PD_8CH：八通道 ADC 采集固件与上位机

本仓库包含 **Cyclone IV E / AD7380 / FT232H 固件**和 **PD 8CH v1.0.0 Windows 上位机**。克隆后可独立构建，不需要原始 `afs_fireware` 或 `afs_gui1.0` 工程。全部 **28 个顶层信号已在 QSF 分配实际管脚**，支持完整 Quartus 编译并生成 SOF。

第一次使用请从 **[首次使用指南：环境安装 → 编译 → 仿真演示 → 下载固件 → 实板采集](doc/getting_started.md)** 开始。没有板卡时，可先运行软件 Simulation 模式。

## 获取源码

在 Windows PowerShell 中执行：

```powershell
git clone https://github.com/houxianfei/PD_8CH.git
cd PD_8CH
```

后续命令均从这个仓库根目录执行，目录名不必是开发机上的 `project_0`。仓库已包含 ImGui/ImPlot、FTDI 开发库和签名驱动，无 Git 子模块。工具链、编译输出、个人 ini、历史安装包不在 Git 中；新 clone 没有 EXE/SOF/安装包，需要先构建。

## 先选择要做的事

| 目标 | 所需环境 | 入口与结果 |
|---|---|---|
| 无板卡体验八通道采集 | Windows x64、VS2019 C++/v142、Windows 10 SDK | 编译软件，选择 Simulation；[演示步骤](software/docs/simulation.md) |
| 编译/运行上位机或调用 SDK | 同上；硬件连接另需 D2XX 系统驱动 | `software/pd_8ch.sln`；[软件说明](software/README.md) |
| 运行 RTL 自动仿真 | Icarus Verilog，已验证12.0 | `scripts/test.ps1`，四个 testbench |
| 编译 FPGA 固件 | Quartus Prime Lite 22.1std.1 Build 917，Cyclone IV 器件支持 | `par/PD.qpf` → `par/output_files/PD.sof` |
| 连接实板采集 | 已配置 FPGA、FT232H 同步245 FIFO、D2XX 驱动 | [下载与联调步骤](doc/getting_started.md#5-下载固件并连接板卡) |
| 生成安装包/免安装包 | 软件工具链及 Inno Setup 6 | `software/scripts/package.ps1` → `software/publish/` |

## 最短运行路径：无硬件演示

安装 VS2019 的“使用 C++ 的桌面开发”、MSVC v142 和 Windows 10 SDK 后执行：

```powershell
./software/scripts/build.ps1 -Configuration Release -Test

# 命令行 SDK 演示：搜索、Get、有限/连续采集、CSV、停止、断开
./software/build/PD_8CH_v1.0.0/Release/pd_8ch_simulation_demo.exe ./software/build/logs/my_demo.csv

# 启动界面
./software/build/PD_8CH_v1.0.0/Release/pd_8ch.exe
```

界面依次选择 **Mode → Simulation → SIM-8CH → Connect**，点击 **Get**；打开 **Tools → realtime_measure**，设置 **100 Hz / Duration=1 s / Continue 未勾选**，选 CH1、CH3 后点击 **Start**。两个通道各画一个子图，有限采集自动停止，再点击 **Save selected channels (CSV)** 导出。仿真不需要安装系统 USB 驱动，保留 EXE 旁的 `ftd2xx.dll` 即可。

## 固件编译入口

```powershell
# 默认 Quartus 路径为 C:/intelFPGA_lite/22.1std/quartus
./scripts/build.ps1 -Mode compile

# 安装在其他目录时，显式传入包含 bin64 的 Quartus 根目录
./scripts/build.ps1 -Mode compile -QuartusRoot 'D:/intelFPGA_lite/22.1std/quartus'
```

顶层为 `PD`，器件为 **EP4CE6E22C8**，约束为 `par/PD.qsf` / `par/PD.sdc`。完整编译先检查管脚缺失/重复，再完成综合、布局布线、汇编及时序分析。最新复核已生成 `PD.sof`；当前构建数据及验证边界见[验证记录](doc/validation.md)。SOF 用于 JTAG 配置；掉电保持的 Flash 文件需按实际板卡配置器件另行转换。

## 硬件与软件约定

PD为独立产品，首版固件/软件均为1.0.0，不导入旧AFS配置。连接后Main UI分别显示软件版本和从F4读取的固件版本；SDK更名对照见[改名说明](doc/pd_rename.md)。


- 四片 AD7380 共八通道；上下层共用 `spics / spiclk / spido`，CH0～CH3 对应 `spidiT[0..3]`，CH4～CH7 对应 `spidiB[0..3]`。[全部引脚及电平说明](doc/hardware.md)。
- ADC 内部每通道1 MSPS同步转换；软件选择的 **1 kHz / 100 Hz / 10 Hz / 1 Hz** 是上传频率。始终上传八通道，勾选只决定显示和 CSV 导出。
- 表格显示 CH、可编辑 Name、原始有符号16位 ADC、Power、P0、P1；校准公式为 **`Power = P1 * (P0 + ADC)`**，默认0/1。
- 每个勾选通道显示独立子图和实时文本。UI设置自动保存到程序旁 `cfg/pd_8ch.ini`；校准文件另行 Save/Load。
- CSV 只包含所选通道的 ADC/Power，记录名称、校准参数、采集起止时间等；采用 UTC 绝对时间戳，仿真数据明确标记来源。[CSV格式](software/docs/csv_format.md)。

## 仓库导航

| 路径 | 内容 |
|---|---|
| `rtl/` | 可综合 Verilog，包含通用 FIFO 和 FT232H 总线逻辑 |
| `par/` | Quartus 工程、引脚、SDC；编译生成的 `output_files/` 被忽略 |
| `sim/` | 四个 RTL 测试平台及已提交的协议黄金向量 |
| `scripts/` | 固件构建、管脚检查、SDC端点检查、RTL仿真 |
| `software/` | C++ GUI、SDK源码、测试、仿真示例、驱动与打包脚本 |
| `doc/` | 整机入门、硬件接口、固件协议与验证记录 |
| `software/docs/` | 软件操作、SDK仿真、CSV、驱动及性能说明 |

详细索引见[文档导航](doc/README.md)。完整管脚约束和编译通过不替代板卡输入、USB枚举、实测采集频率及长期稳定性验证；文档区分已执行的自动验证和待记录的实板结果。
