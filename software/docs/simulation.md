# 无硬件演示与 SDK 调用（v1.4.0）

仿真模式不需要板卡或安装系统 USB 驱动。请保留程序附带的 `ftd2xx.dll`；它是应用程序依赖库，不是系统驱动。安装版的 USB 2.0 Driver 页面可选择 **Skip**，也可直接使用免安装版。

## 在界面中演示

1. 启动 `afs_8ch.exe`。Main UI 的 **Mode** 选择 **Simulation**，自动搜索到 **SIM-8CH**；需要时点击 **Search simulation** 重新搜索。
2. 选择 **SIM-8CH**，点击 **Connect**。界面显示 `SIMULATION` 提示，数据为生成的演示信号。
3. 点击 **Get**，一次读取八个通道。重复点击可观察变化；未开始采集时 Get 不产生可导出的采样缓存。
4. Main UI 将 CH1 的 **Name** 改为 `Demo probe A`，**P0=2**、**P1=3**；Power 和曲线统一使用 `P1 * (P0 + ADC)`。可通过 Save calibration / Save As... 保存文件，再用 Load calibration 恢复。
5. 打开 **Tools → realtime_measure**，选 **100 Hz**，取消 **Continue**，设 **Duration (s)=1.000**。仅勾选 **CH1** 和 **CH3**，点击 **Start**。
6. 两个通道各自显示一个独立子图及 Power、最小值、最大值、PV 文本。约一秒后自动停止，设备保持连接，缓存应为100帧。改选通道可查看同一缓存中其他通道。
7. 点击 **Save selected channels (CSV)**，选择保存位置。仅导出勾选通道；例中为时间戳加两组 ADC/Power，共5列。CSV 记录名称、P0/P1、绝对UTC时间、采集起止信息，并标记 `data_source=simulation`。
8. 勾选 **Continue**，选择 **1 kHz**，点击 **Start** 演示连续采集；点击 **Stop** 后再保存。开始新采集会清除旧缓存，**Clear cache** 也会清空缓存。
9. 点击 **Disconnect**。断开后才能把 Mode 改回 **USB 2.0 / FT232H**。切换模式会清空缓存；需要保留的数据请先导出。Mode 自动保存到 ini，下次恢复选择，但不会自动连接或采集。

支持1 kHz、100 Hz、10 Hz、1 Hz，持续时间与自动停止逻辑和硬件模式一致。八路信号为不同频率、相位、幅度、偏置的确定性正弦波；不用于证明硬件精度、USB吞吐或拔插行为。界面 FPS 与采样频率是不同指标。

## 直接调用 SDK

SDK 公共接口为 `include/afs/afs_driver.h`，无需包含内部传输类。创建模拟 SDK 后，连接、Get、采集、读取、停止和导出接口与硬件模式相同：

```cpp
AFS::af_driver sdk(AFS::DriverMode::Simulation);
sdk.search_devices();
// 轮询 DataCollection_read()，等待 busy=false，再读取 devices。
sdk.open("SIM-8CH");
// 等待 connected=true && busy=false。
sdk.get_device_data();
// 等待 busy=false，读取 channel_values。
sdk.DataCollection_start({100, 1000, false}); // 100 Hz、1000 ms、非连续
// 等待 State::Connected、busy=false、acquisition_end_unix_us>0。
auto capture = sdk.DataCollection_read();
// 用 CsvExportOptions 指定选中通道、名称、P0/P1，再调用 save_realtime_csv。
sdk.close();
```

SDK 操作为异步请求。不要只用固定 sleep 判断完成；轮询 `busy`、状态和 `error`，并设置等待超时。完整可运行示例为 [simulation_demo.cpp](../examples/simulation_demo.cpp)，其中包含等待、错误处理、有限/连续采集、导出、停止后重启、清空和模式切换。

现有默认构造 `AFS::af_driver sdk;` 仍使用USB。也可在断开时调用 `set_connection_mode(AFS::DriverMode::Simulation)`，等待 `busy=false` 后重新搜索并连接。连接期间切换模式会返回错误并保留当前连接。读取快照里的 `driver_mode` 为当前连接模式，`acquisition_mode` 为缓存所属的数据来源。

## 运行命令行示例

解压免安装包后，在该目录的 PowerShell 中执行：

```powershell
./afs_8ch_simulation_demo.exe ./simulation_demo.csv
```

控制台依次显示搜索、连接/Get、100Hz有限采集、选通道校准导出、连续采集/停止/清空/断开和模式切换。成功时退出码为0，CSV包含100帧CH1、CH3模拟数据，不会搜索或打开真实USB设备。SDK示例切到USB模式时仅创建断开的传输对象，不做枚举或连接。

从源码构建：在项目根目录运行 `./software/scripts/build.ps1 -Configuration Release -Test`，或使用 VS2019 打开 `software/afs_8ch.sln`，构建 `afs_8ch_simulation_demo`。输出位于 `software/build/v1.4.0/Release/`。示例工程直接编译本工程驱动源码；本版本未单独提供二进制SDK DLL。

仿真通过独立传输实现真实时间调度、寄存器应答、双组上传帧和CRC，复用 SDK 后台线程、跨读取帧缓存、解析器及采集生命周期。UI未另写一套波形采集逻辑。
