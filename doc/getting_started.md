# 首次使用指南

本文面向第一次 clone 本仓库的开发者，覆盖环境准备、无硬件演示、RTL仿真、完整固件编译、JTAG下载、八通道采集与发布。以下命令使用 Windows PowerShell，均从仓库根目录执行，除非另有说明。

## 1. 获取源码并认识入口

```powershell
git clone https://github.com/houxianfei/PD_8CH.git
cd PD_8CH
git status
```

不需要 `--recurse-submodules`。仓库中已有RTL、测试黄金向量、GUI依赖源文件、FTDI头文件/导入库/DLL及签名驱动。原始参考工程的路径仅用于历史追溯，不是构建依赖。

| 要打开的工程 | 文件 | 作用 |
|---|---|---|
| FPGA | `par/AFS.qpf` | Quartus工程，顶层AFS，器件EP4CE6E22C8 |
| 上位机和SDK例程 | `software/afs_8ch.sln` | VS2019解决方案，选择x64 |
| SDK公共接口 | `software/include/afs/afs_driver.h` | C++驱动、快照、采集、校准和CSV接口 |
| SDK演示源码 | `software/examples/simulation_demo.cpp` | 无硬件执行完整SDK操作流程 |

`.tools/`、`par/output_files/`、`sim/build/`、`software/build/`、`software/publish/`、VS缓存和用户ini被Git忽略。新 clone 没有开发者本机的工具、SOF、EXE或安装包；按下文构建会生成它们。普通测试也无需重新生成 `sim/reference_vectors.vh`。

## 2. 按工作内容安装环境

| 工具 | 何时需要 | 安装/配置要点 |
|---|---|---|
| Git for Windows | clone、更新、提交 | 使用能执行 `git --version` 的终端 |
| PowerShell | 运行脚本 | 本次复核环境为PowerShell 7.6.9；示例使用Windows路径 |
| Visual Studio 2019 或其Build Tools | 编译上位机、SDK例程、软件测试 | 安装“使用C++的桌面开发”、MSVC v142、Windows 10 SDK |
| Quartus Prime Lite 22.1std.1 Build 917 | 编译/下载FPGA固件 | 安装Cyclone IV器件支持和所用JTAG下载器驱动 |
| Icarus Verilog | 运行四个RTL testbench | 已验证12.0；准备包含 `iverilog.exe` 和 `vvp.exe` 的bin目录 |
| Python 3 | 独立CSV校验、可选黄金向量重生成 | 这些脚本仅使用标准库；软件构建及C++测试本身不依赖Python |
| Inno Setup 6 | 生成软件安装包 | 本项目已用6.7.3编译；不参与EXE或固件编译 |
| FTDI D2XX系统驱动 | 连接实际FT232H板卡 | 已随源码提供；仅做Simulation时可不安装 |

工具获取入口：[VS旧版本](https://visualstudio.microsoft.com/vs/older-downloads/)、[Quartus Lite 22.1.1 Windows](https://www.altera.com/downloads/fpga-development-tools/quartus-prime-lite-edition-design-software-version-22-1-1-windows)、[Icarus Windows发行包](https://bleyer.org/icarus/)、[Inno Setup下载](https://jrsoftware.org/inno-setup/downloads)。工具版本按本工程已验证组合选择，下载站首页展示的新版不等同于本工程已验证版本。

软件脚本用 `vswhere -version '[16.0,17.0)'` 查找VS2019。仅安装VS2022/更新版，即使有v142组件，也不会被当前脚本自动选中；请安装VS2019工具链。SDK版本在vcxproj中为 `10.0`，由本机已安装Windows 10 SDK解析。

Quartus脚本默认安装目录为 `C:/intelFPGA_lite/22.1std/quartus`。若路径不同，每次用 `-QuartusRoot` 指定，值应是包含 `bin64` 的 `quartus` 目录。Icarus脚本默认查找 `.tools/iverilog/bin`，而 `.tools/` 不随clone提供，因此新环境通常应显式传 `-IcarusBin`。

## 3. 先在无硬件条件下运行软件

### 3.1 构建、测试并定位输出

```powershell
./software/scripts/build.ps1 -Configuration Release -Test
```

脚本构建三个项目：GUI `afs_8ch`、测试 `afs_8ch_tests`、命令行SDK演示 `afs_8ch_simulation_demo`，然后执行C++测试和SDK演示。输出：

| 路径 | 内容 |
|---|---|
| `software/build/v1.4.0/Release/afs_8ch.exe` | GUI程序 |
| `software/build/v1.4.0/Release/afs_8ch_simulation_demo.exe` | SDK操作示例 |
| `software/build/v1.4.0/Release/ftd2xx.dll` | 随构建复制的FTDI用户态库 |
| `software/build/logs/build_Release.log` | MSBuild日志 |
| `software/build/logs/test_Release.log` | C++测试结果，成功末行为ALL TESTS PASSED |
| `software/build/logs/sdk_demo_Release.log`、`.csv` | SDK演示输出与100帧模拟数据 |

EXE要与 `ftd2xx.dll` 一起使用，Simulation也需保留此DLL。系统D2XX驱动和应用DLL是不同层次的依赖。

### 3.2 界面演示

```powershell
./software/build/v1.4.0/Release/afs_8ch.exe
```

1. 在Main UI将 **Mode** 选为 **Simulation**，选中 **SIM-8CH**，点击 **Connect**。界面明确显示SIMULATION。
2. 点击 **Get** 更新全部八通道ADC/Power。Get只更新当前值，不产生可导出的采样缓存。
3. 修改CH1的 **Name=Demo probe A、P0=2、P1=3**。表格和曲线统一按 `Power=P1*(P0+ADC)` 计算。
4. 打开 **Tools → realtime_measure**，选择 **100 Hz**，取消 **Continue**，设 **Duration (s)=1.000**，只勾选CH1、CH3。
5. 点击 **Start**。应显示两个独立子图，采集100帧后自动停止，连接保持Connected。每个子图显示当前Power、最近显示范围的最小/最大值和PV。
6. 点击 **Save selected channels (CSV)**。此例保存5列：绝对UTC时间、CH1 ADC/Power、CH3 ADC/Power；名称、校准参数、采集起止和 `data_source=simulation` 写入元数据。
7. 勾选 **Continue** 后重新Start可演示连续采集；点击 **Stop** 排空残留，再保存或重启。**Start和Clear cache会丢弃上次缓存**，需要保留时先保存。
8. 点击 **Disconnect** 后可改回USB模式。切换模式也会清空缓存。参数自动保存，重新打开后恢复参数，但不会自动连接/采集。

更多步骤和SDK接口说明见[Simulation说明](../software/docs/simulation.md)。Main UI右上角X退出程序并释放采集资源；realtime_measure右上角X仅隐藏曲线窗口。

### 3.3 不打开界面，直接调用SDK

```powershell
./software/build/v1.4.0/Release/afs_8ch_simulation_demo.exe ./software/build/logs/my_demo.csv
```

成功时控制台打印六步操作并返回0。例程构造 `AFS::af_driver(AFS::DriverMode::Simulation)`，通过公开API搜索、连接、Get、有限/连续采集、导出、清空、断开及切换模式。所有异步操作都等待状态并检查错误，不搜索或打开真实USB设备。此例程直接编译SDK源码，仓库未单独发布SDK DLL。

## 4. 验证并编译固件

### 4.1 RTL仿真

将下面的Icarus路径改为本机实际目录：

```powershell
./scripts/test.ps1 -IcarusBin 'C:/iverilog/bin'
```

成功时依次输出 **PASS tb_adc、PASS tb_register、PASS tb_core、PASS tb_ft232h**；中间的 `$finish` 是正常仿真结束。四个testbench分别验证ADC配置/同步采集、AXI寄存器、命令与上传流程、完整顶层和FT232H总线。临时产物位于 `sim/build/`。

如果自行把工具放在 `.tools/iverilog/bin`，可直接执行 `./scripts/test.ps1`。无需Questa运行许可证；本仓库自动流程使用Icarus。

### 4.2 完整编译并生成SOF

```powershell
./scripts/build.ps1 -Mode compile

# 非默认安装位置的等价写法
./scripts/build.ps1 -Mode compile -QuartusRoot 'D:/intelFPGA_lite/22.1std/quartus'
```

两条命令选择符合本机路径的一条即可。该流程依次完成：管脚检查、Analysis & Synthesis、Fitter、Assembler、Timing Analyzer。当前所有管脚已分配，**完整编译是正常支持的流程**，不再预期因下层板缺管脚而失败。

也可用Quartus打开 `par/AFS.qpf`，确认Revision=AFS、Top-level entity=AFS、Device=EP4CE6E22C8，再点 **Processing → Start Compilation**。GUI同样执行PRE_FLOW管脚检查。

| 生成文件 | 用途 |
|---|---|
| `par/output_files/AFS.sof` | JTAG配置FPGA |
| `AFS.flow.rpt` | 完整流程状态 |
| `AFS.fit.summary` / `AFS.fit.rpt` | 布局布线和资源占用 |
| `AFS.pin` | 最终管脚报告，可与QSF和原理图对照 |
| `AFS.sta.summary` / `AFS.sta.rpt` | 布局布线后的时序汇总和详情 |

表中短文件名均在 `par/output_files/` 下。脚本成功返回后，还应阅读时序和约束报告；当前已知报告项和数值见[验证记录](validation.md)。

### 4.3 仅综合与SDC端点检查

```powershell
./scripts/build.ps1 -Mode synth
./scripts/build.ps1 -Mode timing
```

timing模式检查三个时钟、128个ADC捕获寄存器和FIFO Gray指针约束端点，并生成 `AFS.clocks.rpt`、`AFS.cdc_nets.rpt`。脚本请求 `-post_map`；干净综合后使用综合网表，如果已经运行Fitter，Quartus会提示忽略-post_map并使用可用网表。这是约束端点检查，不是完整时序签核，不能替代compile生成的STA报告。更换安装路径时两条命令都加 `-QuartusRoot`。

## 5. 下载固件并连接板卡

### 5.1 用JTAG配置FPGA

准备板卡电源和JTAG下载器（例如USB-Blaster），安装下载器自身驱动。**JTAG下载器驱动与FT232H采集用D2XX驱动不同**。

1. 完成上一节编译，确认 `par/output_files/AFS.sof` 是本次源码生成的文件。
2. 在Quartus **Tools → Programmer** 中选择对应Hardware，模式选JTAG，点击Auto Detect。
3. 确认检测器件与EP4CE6E22C8一致，给该器件选择 `AFS.sof`，勾选Program/Configure后执行Start。
4. 观察配置完成，再复位板卡；确认50MHz系统时钟、FT232H 60MHz FIFO时钟和低有效复位符合[硬件接口](hardware.md)。

SOF配置是易失的，断电后消失。若需掉电自动启动，在Quartus Convert Programming Files中按**实际配置Flash型号、模式和板卡接法**转换JIC/POF等文件；本仓库没有通用Flash转换配置，build脚本只保证生成SOF。不要把其他电脑的CDF绝对路径或旧JIC文件当成本次构建结果。

### 5.2 安装采集USB驱动

驱动已在 `software/driver/CDM/`，不必再次下载。可用设备管理器更新FT232H驱动并选择该目录，或在仓库根目录的**管理员PowerShell**执行：

```powershell
pnputil.exe /add-driver './software/driver/CDM/ftdibus.inf' /install
```

安装版也会显示独立 **USB 2.0 Driver** 选择页。使用FTDI D2XX/CDM 2.12.36.4，不选COM端口；不是FT601的D3XX，也不是WinUSB/libusb。仅做Simulation时可以跳过系统驱动安装。详细说明见[USB驱动安装](../software/docs/USB_DRIVER_INSTALL.txt)。

### 5.3 首次实板采集

1. 确认板卡运行本仓库固件，ADC与USB接线按完整QSF；板卡供电并用USB数据线连接FT232H。
2. 启动上位机，断开现有仿真连接，将 **Mode** 改为 **USB 2.0 / FT232H**。
3. 点击 **Search USB**，选择实际序列号，再 **Connect**。这里只枚举FT232H；若连接失败先看界面英文错误提示。
4. 点击 **Get** 读取全部八个寄存器。默认P0=0、P1=1，Power应与ADC相同；输入已知信号检查通道映射。
5. 先以100Hz、1秒有限采集确认100帧、八通道及CRC/跳号计数，再尝试1kHz或Continue。有限模式结束后应保持连接；Stop后可以再次Start。
6. 需要导出的通道保持勾选，停止后保存CSV以取得正式采集结束时间。采集中也可保存快照，此时acquisition_end字段为空，data_start/data_end仍填写。

寄存器读取在空闲时逐个完成，不保证八个Get值来自同一次ADC转换；上传帧才是同步八通道快照。采集中Get复用最新完整帧，不发并发寄存器读命令。详细协议见[固件协议](firmware_protocol.md)。

建议记录板卡编号、FPGA提交号、软件版本、USB序列号、采集设置、输入信号及CSV，再记录频率、通道、停止/重启、拔插恢复和长时间采集结果。自动仿真或编译通过不等于这些实板项目已执行。

## 6. 理解保存文件和采样设置

| 文件/设置 | 含义 |
|---|---|
| 1MSPS | ADC内部转换速率，不是UI上传频率 |
| 1kHz / 100Hz / 10Hz / 1Hz | USB上传频率；模拟模式也按实际时间调度 |
| 通道勾选 | 控制独立子图和CSV列；内部始终缓存全部八通道 |
| `cfg/afs_8ch.ini` | EXE旁自动保存UI参数、模式、窗口布局；重新打开自动恢复 |
| `cfg/calibration.cfg`或自选文件 | Save calibration/Save As导出的名称与P0/P1；Load calibration可加载 |
| CSV | 当前完整保留缓存中的所选通道，ADC与校准后的Power，名称、参数、绝对UTC与采集元数据 |

开发构建的cfg在EXE所在的 `software/build/v1.4.0/Release/` 下，不是源码中的 `software/cfg/`；后者提供发布默认文件。源码/便携版放在可写目录，安装版为程序cfg目录设置写权限。

缓存上限65536帧，超出后覆盖最早数据。绘图最多显示最近1500帧，CSV可保存全部保留缓存。导出时使用当前名称和校准系数重新计算Power。绝对UTC为主机开始时间加设备相对计时；它不是额外硬件授时精度声明。[CSV详情](../software/docs/csv_format.md)。

## 7. 完整软件验证与发布

```powershell
./software/scripts/build.ps1 -Configuration All -Test
python ./software/scripts/verify_csv.py ./software/build/logs/csv_Debug
python ./software/scripts/verify_csv.py ./software/build/logs/csv_Release
./software/scripts/ui_qa.ps1 -Configuration Release
```

前两类测试覆盖协议、USB生命周期替身、公开仿真SDK、ini、校准与CSV；ui_qa使用D3D11 WARP离屏绘图，不控制桌面，截图在 `software/build/ui_qa/`。SDK命令行演示在-Test时已经执行。

安装Inno Setup 6后打包：

```powershell
./software/scripts/package.ps1

# 编译器安装路径不同时显式指定
./software/scripts/package.ps1 -InnoCompiler 'D:/Tools/Inno Setup 6/ISCC.exe'
```

选择符合本机路径的一条。默认编译器路径为 `$env:LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe`。脚本重新执行Release构建/测试，再生成：

- `software/publish/AFS_8CH_Setup_v1.4.0.exe`
- `software/publish/AFS_8CH_v1.4.0_portable.zip`
- `software/publish/AFS_8CH_v1.4.0/` 完整程序目录
- `software/publish/SHA256.txt`

软件包含GUI、SDK演示、FTDI应用DLL、完整签名驱动、许可证、默认cfg及软件文档，**不包含FPGA SOF**。若要交付可直接联调的一套材料，应另附由同一提交编译的SOF及提交号。publish在.gitignore中，`git push`不会上传这些安装包；如需GitHub下载，应另行发布Release附件。

## 8. 常见问题

| 现象 | 从哪里检查 |
|---|---|
| clone后没有EXE/SOF/安装包 | 正常；按第3/4/7节构建，被忽略的开发机产物不会跟随clone |
| 提示需要VS2019/v142 | 用VS Installer检查C++工作负载、v142和Windows 10 SDK；当前脚本不自动选择VS2022 |
| 缺少ftd2xx.dll | 保留EXE同目录的DLL，或重新构建；仅安装系统驱动不能替代此文件 |
| 找不到iverilog.exe | 指定 `-IcarusBin`，同时确认目录内有vvp.exe；.tools不在Git中 |
| 找不到quartus_sh.exe或器件 | 传正确的-QuartusRoot，安装Cyclone IV器件支持 |
| 提示缺管脚/重复管脚 | 运行check_pins检查QSF本地修改；当前提交的28个管脚已完整分配 |
| 搜索不到FT232H | 检查D2XX、供电/线缆、设备类型、其他程序占用；先用Simulation排除GUI问题 |
| 已连接但Get/上传超时 | 检查FPGA配置、50/60MHz时钟、复位、FT232H FIFO布线/EEPROM和协议版本 |
| 采集中两秒无有效帧断开 | 连续模式的超时保护；有限模式按时长/帧数自动停止，不应因此断开 |
| 保存CSV按钮不可用 | 先采集产生缓存，至少勾选一个通道；Get不产生采样缓存 |
| ini没有恢复/保存失败 | 检查EXE旁cfg目录可写；损坏ini会先备份为.invalid文件，界面提示状态 |
| 软件采样频率与FPS不同 | 上传频率、ADC1MSPS和UI约60FPS是三个不同指标 |

如果PowerShell提示禁止执行脚本，可在当前终端按本机策略允许后再运行；例如 `Set-ExecutionPolicy -Scope Process Bypass` 仅作用于当前进程。路径含空格时用引号，直接执行带引号的EXE路径时在前面加 `&`。

## 9. 修改和维护入口

固件实现说明见[协议与源码职责](firmware_protocol.md#实现入口)，软件入口为 `software/app/afs_gui.cpp`、UI在 `software/src/ui/`、SDK/USB/仿真传输在 `software/src/driver/`。新增接口时同步维护QSF、SDC、管脚检查、仿真和文档，保留既有顶层命名与通道顺序。

只有维护黄金向量时才需要额外参考仓库；普通构建不需要。已准备固定提交 `ae1a285` 后可显式运行：

```powershell
python ./scripts/reference_vectors.py --reference 'D:/reference/afs_fireware' --icarus-bin 'C:/iverilog/bin'
```

该脚本从参考RTL提取CRC函数生成向量。重新生成后先检查差异，再运行RTL和软件测试，避免把协议回归隐藏在新的期望值里。
