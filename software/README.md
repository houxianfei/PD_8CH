# PD 8CH v1.0.0

八通道 USB 2.0 信号采集软件，基于 `E:\AFS\afs_gui1.0\afs_gui` 的 C++ / Dear ImGui / ImPlot 技术栈精简开发。保留旧版 `realtime_measure` 的独立窗口、纵向子图和实时文本形式。

第一次clone请先看[整机首次使用指南](https://github.com/houxianfei/PD_8CH/blob/main/doc/getting_started.md)。上述开发机路径只是来源记录；构建需要的依赖已在本仓库，无需取得原GUI工程。新clone没有build/publish产物，先构建再运行；当前完整固件引脚表见[硬件接口](https://github.com/houxianfei/PD_8CH/blob/main/doc/hardware.md)。

## 使用

连接后Main UI分别显示Software version和Firmware version。当前软件和固件均为1.0.0；固件版本从只读寄存器0xF4读取，Simulation标记为simulated。未连接显示Not connected，不支持版本寄存器时显示Unknown。版本查询超时或应答错误按连接失败释放资源。


**没有硬件时**：Main UI 的 **Mode → Simulation**，选择 **SIM-8CH → Connect**。Get、有限/连续采集、逐通道绘图、校准和CSV均可演示，无需安装系统USB驱动。完整界面步骤与SDK调用见 [仿真演示说明](docs/simulation.md)，也可运行 `pd_8ch_simulation_demo.exe simulation_demo.csv`。模拟数据在UI及CSV中明确标记。默认仍为USB模式，模式自动保存，断开后才能切换。

1. 按 [USB 驱动说明](docs/USB_DRIVER_INSTALL.txt) 安装 D2XX 驱动，连接运行本项目固件的 FT232H 板卡。
2. 启动 `pd_8ch.exe`，点击 **Search USB**，选择设备序列号后点击 **Connect**。
3. 通过 **Tools → realtime_measure** 打开采集窗口。
4. 选择 **1kHz、100Hz、10Hz、1Hz**，设置**Duration (s)**（0.100～60.000秒），或勾选 **Continue**，然后点击 **Start**。
5. 勾选需要查看的 CH0～CH7。选择一个通道显示一个独立子图，选择两个显示两个，按通道编号从上到下排列；未选择通道时不显示图。选择同时控制绘图和CSV导出；USB上传和内部缓存仍保留八通道。
6. 每个子图实时显示 `名称 Power = 校准值, range[最小值, 最大值], PV = 最大值-最小值`。统计范围为绘图窗口最近 1500 个样本；X 轴联动，各图 Y 轴独立缩放。小窗口可滚动，窗口可移动、缩放。
7. **Stop** 停止并排空残留数据。**Save selected channels (CSV)** 保存确认文件名时选中通道的完整保留缓存快照，默认目录为用户文档中的 `PD 8CH`。保存后也可继续采集。

主界面表格固定八行，列为 `CH / Name / ADC / Power / P0 / P1`。`Name` 可逐通道编辑，同步到绘图选择和图内文本；CH 编号与硬件映射不随名称改变。名称相同也保持独立通道；名称留空时，绘图回退显示 CH 编号。

`Get` 一次更新全部八通道：空闲时顺序读取 0x00～0x07 寄存器，全部成功后一起刷新表格（它们不保证来自同一次 ADC 转换）；采集中取最新完整上传帧，不中断采集、不混入寄存器回复。Get 不追加缓存帧，也不改变现有曲线时间戳。

校准公式为 `CH_Power[n] = P1[n] * (P0[n] + CH_ADC_value[n])`。默认 P0=0、P1=1；修改后，表格 Power、实时曲线及其当前值/范围/PV 都使用当前系数，历史缓存也以当前系数重新显示。`Save calibration` 将名称和系数导出到界面显示的校准文件，默认是程序旁的 `cfg/calibration.cfg`；`Save As...` 可选文件名；`Load calibration` 只加载带 `PD8CH_CALIBRATION_V2` 文件头的PD `.cfg` 文件。完整路径始终显示在 Main UI。`Reset P0/P1` 只恢复系数，不清除名称。系数不写入 FPGA。AFS及其他旧格式会被拒绝，当前有效系数保持不变。

界面自身的文字均为英文。外层 Win32 宿主窗口隐藏，仅显示独立 Main UI 和 realtime_measure 窗口；绘图没有额外的填色背景。Main UI 右上角 X 会停止采集、释放 USB 并退出；绘图窗口 X 只关闭绘图，可从 Tools 重新打开。Main UI 底部固定显示界面 FPS 和 ms/frame，不代表 USB 采样频率。

CH0～CH3 对应 `spidiT[0]～spidiT[3]`，CH4～CH7 对应 `spidiB[0]～spidiB[3]`。ADC 列按有符号16位原始码显示，Power 列按上述线性校准公式计算。不预设能量单位，由用户的校准系数决定。

**Start 会清空上次缓存**；缓存上限为 65536 帧，连续采集超出上限后覆盖最早样本，界面显示累计接收数和覆盖数。CSV 保存整个保留缓存，而非仅保存图上最近 1500 帧。清空前请先保存需要保留的数据。

v1.0.0 的 GUI CSV 只导出选中的通道，每个通道包含带名称的 ADC 和 Power 两列。时间列改为绝对 UTC 时间戳 `timestamp_utc`（ISO 8601、六位小数、带Z），文件头记录采集/数据的开始结束时间、采样设置、通道选择、实际P0/P1、校准公式/文件路径及错误计数。例如选择 CH1 / Probe A 和 CH3 / Probe B：

```text
timestamp_utc,"CH1 [Probe A] ADC","CH1 [Probe A] Power","CH3 [Probe B] ADC","CH3 [Probe B] Power"
```

完整格式及时间精度说明见 [CSV说明](docs/csv_format.md)。采集中导出会标记Collecting，数据起止时间仍完整记录；正式采集结束时间在停止后保存时提供。绝对时间基于主机启动命令UTC加FPGA相对时间，没有额外硬件UTC授时。

本版没有原自动对焦、杂散光标定、设备参数 cfg、电机、运动平台、光源、压电、固件升级、PCIe或以太网业务。新增软件线性校准、通道名称及无硬件仿真入口。原 GUI 工程及历史测量数据未复制、未改动。

保存任务固定使用确认保存时的完整缓存、通道选择、名称和系数快照；后续修改系数不改变正在保存的文件。SDK另提供原始数据CSV接口，可输出九列；GUI使用PD元数据格式。

## 自动恢复与有限采集

所有可设置的业务参数自动写入程序旁的 `cfg/pd_8ch.ini`：连接模式、频率、采集秒数、Continue、绘图窗口开关、八个通道勾选、名称、P0/P1、设备选择、校准文件路径、CSV目录。窗口位置、大小、折叠状态及表格列宽也保存在同一ini。修改后约0.5秒保存，正常关闭时再完整保存一次；无需手动保存，界面仅显示自动保存状态，不显示默认ini路径。

启动时恢复ini中上次使用的设置（包括尚未另存为校准文件的系数编辑），不会自动连接USB或自动开始采集。ADC数值、采样缓存、实时状态和自动计算的坐标轴范围属于运行数据，不恢复。校准cfg文件用于明确导入/导出，与自动恢复的UI ini分开；点击Load后会立即更新表格和曲线，并进入下一次ini自动保存。

未勾选Continue时，Settings区域独立显示采集秒数、预计样本数；启动后显示进度。达到目标帧数或目标时间戳会自动停止。若末帧丢失或CRC失败，最迟在设置时长加约0.5秒并排空后结束，显示缺帧提示并保留USB连接，能够直接再次采集。不会再因为有限采集结束后两秒没有数据而断开。真实拔线、USB读写失败仍会报错并释放句柄；连续采集的两秒无有效帧保护保留。

安装版为 `cfg` 和 `data` 目录配置可写权限；安装、升级均保留已有文件（含子目录），仅补充缺失的默认文件，卸载也保留这两个目录及内容。CSV仍保存到界面所选目录。便携版请解压到可写目录。损坏的ini会先备份为同目录的 `pd_8ch.ini.invalid.*` 文件，再自动用默认设置重建；备份失败时保留原文件并提示保存受阻。PD作为独立产品，不读取旧AFS ini或AppData配置，不自动迁移旧系数。首次运行采用默认设置。

## 构建及验证

需要 Windows x64、Visual Studio 2019 C++ 桌面开发工具（v142）和 Windows 10 SDK。

脚本通过vswhere只选择VS2019（16.x），不能仅安装VS2022后直接照用脚本。三个解决方案项目分别是GUI、测试和SDK命令行演示；离屏UI验证工程由ui_qa.ps1单独构建。默认Release构建后可运行：

```powershell
./software/scripts/build.ps1 -Configuration Release -Test
./software/build/PD_8CH_v1.0.0/Release/pd_8ch.exe
```

保留EXE旁的ftd2xx.dll。开发构建自动ini位于EXE旁cfg目录；源码software/cfg用于提供发布默认配置，不是运行时自动ini目录。

```powershell
# 在克隆后的仓库根目录执行，编译并运行 Debug / Release 测试
./software/scripts/build.ps1 -Configuration All -Test

# 不需要硬件或操作桌面的图形渲染验证
./software/scripts/ui_qa.ps1

# Release 测试通过后打包，需 Inno Setup 6
./software/scripts/package.ps1
```

工程入口 `software/pd_8ch.sln`。程序输出在 `software/build/PD_8CH_v1.0.0/Release/`；安装包和免安装压缩包输出在 `software/publish/`。内部协议/生命周期测试、离屏绘图测试均不链接实际 USB 实现，不能触发硬件操作。

工具安装、非默认路径参数、CSV独立校验、实际硬件连接和打包输出详见[首次使用指南](https://github.com/houxianfei/PD_8CH/blob/main/doc/getting_started.md)。publish目录不提交Git；当前源码仓库不包含可直接下载的安装包。使用者可本机构建，维护者也可另外发布GitHub Release附件。

查看 [协议说明](docs/protocol.md)、[验证记录](docs/validation.md)、[驱动来源及校验值](driver/SOURCE.json)、[基线文件清单](docs/source_manifest.json)。

## 驱动与第三方依赖

本版使用 FTDI D2XX / CDM **2.12.36.4** 的 x64 头文件、导入库和 DLL。USB 2.0 是连接制式，不是驱动版本号；本软件不用 FT601 的 D3XX 驱动，也不使用虚拟串口通讯。

主站下载返回 403 后，从 FTDI China D2XX 页面所链接的包下载，保留原始 ZIP、完整签名驱动包和许可证。程序目录的 `ftd2xx.dll` 是原始 `amd64/ftd2xx64.dll` 的重命名副本，符合驱动 INF 中的 x64 安装命名；文件内容未修改。安装包会显示独立的USB 2.0 Driver选择页，默认推荐安装随包驱动；已有可用D2XX驱动时可明确选择Skip。完整签名驱动位于程序旁的driver/CDM目录，也可手动安装。

ImGui / ImPlot 文件来自指定旧版工程，许可证保持原样。未引入新的 GUI 框架或 Xilinx 依赖。

## 帧率与性能

采用高精度60Hz帧调度，仅绘制两个可见窗口。UI每帧获取最近1500帧的轻量快照，CSV保存仍获取完整缓存；校准数据和统计值按数据变化更新，设置仍自动保存。详细阶段计时、优化前后对比与复现方法见 [性能报告](docs/performance.md)。启动时加 `--profile` 可在 `cfg/ui_performance.csv` 记录实际窗口Present、绘图与等待的耗时；普通启动不生成诊断文件。
