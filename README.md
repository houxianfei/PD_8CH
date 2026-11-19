# 8 通道 AD7380 / FT232H 采集工程

配套上位机位于 [`software/`](software/README.md)：AFS 8CH v1.3.0，USB 2.0 / D2XX，支持 1kHz、100Hz、10Hz、1Hz 上传、八通道数据表格、按所选通道数量生成独立实时子图及 CSV 保存。软件工程与原 GUI 工程独立，构建和驱动安装说明见其 README。

目标器件为 **Cyclone IV E EP4CE6E22C8**，使用 **Quartus Prime Lite 22.1std.1**。顶层为 `AFS`，工程入口为 `par/AFS.qpf`。工程只包含 ADC 采集、寄存器管理、USB 通讯及所需的复位/FIFO 逻辑。

参考基线为 `E:\houxf\afs_fireware` 的 `afs_v1` 分支，固定提交 **ae1a285**。`rtl/ftdi_245/` 的全部 Verilog 文件来自该提交，未修改内核；芯片选择在顶层设为 `FTx232H`。`spi_master` 和 `USB_axi4lite` 保留参考接口和状态机，完成 Quartus 声明顺序、复位及无用信号清理。`Sensor_Collection`、`control_register` 和 `packet` 按本项目的八通道需求精简。

## 硬件接口与通道

上层板、下层板各有 4 通道，每板使用 2 片 AD7380 双线输出。上下层板共用一套控制输出 `spics`、`spiclk`、`spido`；输入分别为 `spidiT[3:0]`、`spidiB[3:0]`，8 通道同步采集。

| 通道 | 顶层输入 | FPGA 管脚 | 寄存器地址 |
|---|---|---|---|
| CH0 | spidiT[0] | PIN_71 | 0x00 |
| CH1 | spidiT[1] | PIN_73 | 0x01 |
| CH2 | spidiT[2] | PIN_75 | 0x02 |
| CH3 | spidiT[3] | PIN_76 | 0x03 |
| CH4 | spidiB[0] | **待补** | 0x04 |
| CH5 | spidiB[1] | **待补** | 0x05 |
| CH6 | spidiB[2] | **待补** | 0x06 |
| CH7 | spidiB[3] | **待补** | 0x07 |

共用控制线：`spics=PIN_99`、`spiclk=PIN_101`、`spido=PIN_104`。系统时钟 `clk50M=PIN_23`，FT232H 时钟 `usb_clk_60m=PIN_25`，低有效复位 `Rst_n=PIN_70`。USB 管脚保持旧工程位置，详见 QSF。旧工程中未使用的引脚改为输入三态。

QSF 沿用原工程的默认 **2.5 V** I/O 设置。正式上板前应依据板卡 VCCIO 和器件接口电平确认各 Bank 的 I/O standard，并填写下层板四根数据输入的实际位置；这里没有猜测接线或生成可烧录文件。

## 数据路径与采集

```text
AD7380 ×4 → Sensor_Collection → 8×16bit 同步快照
                                      ├→ AXI4-Lite 寄存器 ← USB_axi4lite ← 命令拼接 ← FT232H RX
                                      └→ packet → 采样 FIFO ─┐
                                            寄存器读应答 ───┴→ 整帧仲裁 → FT232H TX
```

- ADC 工作在 50MHz 系统时钟域。SPI 为 mode 2、25MHz，每次转换读取每根数据线的 16bit，每 50 拍启动一次转换，8 通道均为 **1 MSPS**。
- 复位后等待 600000 拍（12ms），再用 2048 拍完成参考初始化流程，依次写 `0xA000`、`0x9282`；正常转换发送零。保留参考配置的过采样/输出模式。
- 初始化阶段输出零且 `valid=0`；转换完成时，128bit 数据同时锁存，`valid` 输出一个系统时钟脉冲。寄存器和上传模块均读取这个完整快照。
- USB 协议、AXI4-Lite、打包和寄存器工作在 50MHz 域；FT232H 总线工作在独立的 60MHz 域，使用参考内核的异步 FIFO 跨域。
- FT232H 使用同步 245 FIFO 模式，外部总线宽度 8bit；内部 AXI Stream 为 64bit，TX/RX 异步 FIFO 各 256 字。采样输出 FIFO 为 256 个 65bit 存储字（64bit 数据加帧尾），另有一个输出预取寄存器。全部 FIFO 使用可综合 Verilog，不依赖 Xilinx IP。
- 上传是对最新 ADC 快照的定时抽取，**不是逐个发送全部 1 MSPS 转换结果**。采样 FIFO 满时暂停产生新上传帧，ADC 仍持续采集；恢复后取得最新快照，不回补这期间的转换。

## 寄存器

USB 的 8bit 寄存器编号在内部映射到 AXI 地址 `0x40000000 + 编号×4`。

| 编号 | 内容 | 属性 | 复位值 |
|---|---|---|---|
| 0x00～0x07 | CH0～CH7 原始 ADC，低 16bit 有效，高位补零 | 只读 | 0 |
| 0xF0 | 上传间隔，以 50MHz 时钟拍为单位 | 读写 | 1000 |
| 0xF1 | 上传控制 | 读写 | 0 |
| 0xF2 | 有限模式的上传帧数 | 读写 | 0 |
| 0xF3 | 上传完成标志，bit0 有效 | 只读 | 0 |

`0xF0` 回读原写入值，实际间隔使用 `max(写入值,1000)`。沿用参考计数规则，一个间隔为该值加一拍；默认约 20.02µs。第一帧在启动后立即生成，后续按间隔抽取；FIFO 空间不足时等待之后的采样时刻。

`0xF1` 位定义：

| 位 | 含义 |
|---|---|
| bit0 | 启动/停止。有限帧完成后自动清零 |
| bit1 | 0：有限帧数，1：持续上传 |
| bit2 | 清空采样输出 FIFO 的请求，完成后自动清零 |
| bit8 | 上传组0：CH0～CH3 |
| bit9 | 上传组1：CH4～CH7 |
| 其余位 | 保留，写入屏蔽、回读零 |

`0xF2=0` 且为有限模式时不生成数据；两个组都未选择时生成仅含帧头的帧。有限数量按实际完整写入采样 FIFO 的帧计数，不按 ADC 转换次数计数。`0xF3` 在完成或停止时置位，新启动命令将其清零；完成表示生成结束，主机仍应读完已缓存的帧。

停止命令允许正在生成的帧完成。清空请求会等待已暴露给发送端的完整采样帧结束，再丢弃采样输出 FIFO 中尚未发送的内容；寄存器应答不受清空影响。该请求**不清除已进入 FTDI 内核、FT232H 或主机驱动的数据**。建议主机停止后排空残留数据，再开始下一次采集。

ADC 寄存器及未实现地址写入均忽略，未实现地址读零；AXI 写响应返回 OKAY，支持独立 AW/W 握手和字节选通。

## USB 协议

保留 `afs_v1` 的二进制布局及 CRC 运算，移除升级、对焦、校准、执行器等业务语义。原校准数据的地址 `0x04～0x07` 和数据组1，在本项目中改为 CH4～CH7；上位机应按本项目寄存器表解释。

每条命令固定 **8 字节**，按下面的线缆顺序发送：

```text
AA CMD ADDR DATA0 DATA1 DATA2 DATA3 CRC
```

`CMD=01` 写、`CMD=02` 读；32bit 写数据低字节在前，读命令的数据字节全部填零。CRC 初值为0，多项式 `0x07`；必须按参考 RTL 的 **64bit 值从 bit63 到 bit0** 计算，不能直接对线缆字节顺序调用常规 CRC8。读命令的 CRC 计算忽略 DATA 字段。错误帧/CRC 无应答，丢弃整条命令；下一条仍以8字节为单位接收。USB 分段可任意拆分或合并完整命令，拼接器保留未满8字节的内容。

写命令无 USB 应答。读应答为 **8 字节**：32bit 寄存器值以小端格式重复两次，与参考实现一致。

指定提交生成的字节示例：

| 操作 | 线缆字节，十六进制 |
|---|---|
| 读取 CH7 | `AA 02 07 00 00 00 00 63` |
| 上传间隔设置1000 | `AA 01 F0 E8 03 00 00 54` |
| 有限帧数设置1 | `AA 01 F2 01 00 00 00 E6` |
| 启动双组有限上传 | `AA 01 F1 01 03 00 00 FD` |

一帧上传由一个64bit帧头和选中的数据组组成。帧头的 RTL 拼接为：

```verilog
{8'hAA, group_mask[7:0], timestamp[31:0], crc[7:0], 8'h00}
```

每个64bit字均低字节先上线。因此帧头的实际线缆顺序为：`00 CRC TS0 TS1 TS2 TS3 MASK AA`。组0按 `{CH0,CH1,CH2,CH3}` 拼接，组1按 `{CH4,CH5,CH6,CH7}` 拼接；组0的线缆顺序从 CH3 的低字节开始。组号按0、1顺序发送，不带单独组标签。时间戳在每次启动时归零，每经过一个上传间隔递增；背压期间继续计时，允许时间戳跳号。

上传 CRC 先对 CRC 字节置零的64bit帧头运算，再依次累积选中的64bit数据组，运算方向与参考 `packet.v` 相同。整帧仲裁保证采样帧不被寄存器应答插断；帧间优先发送寄存器应答。

## 构建、测试与约束

在项目根目录使用 PowerShell：

```powershell
# 自动仿真；默认使用本项目 .tools/iverilog/bin，也可显式提供工具位置
./scripts/test.ps1
./scripts/test.ps1 -IcarusBin C:/iverilog/bin

# Quartus 综合，允许下层板四根输入尚未分配位置
./scripts/build.ps1 -Mode synth

# 综合后检查 SDC 时钟、ADC 捕获端点和异步 FIFO 指针网络约束
./scripts/build.ps1 -Mode timing

# 正式编译；当前应因下层板四根 ADC 输入没有实际引脚而失败
./scripts/build.ps1 -Mode compile
```

`-QuartusRoot` 可指定其他 Quartus 安装目录。GUI 中打开 `par/AFS.qpf`，可运行 Analysis & Synthesis；Start Compilation 同样执行 QSF 的 `PRE_FLOW_SCRIPT_FILE` 检查。检查还拒绝重复管脚；不要以自动分配位置替代实际板卡接线。

`AFS.sdc` 定义50MHz/60MHz时钟、25MHz门控 SPI 生成时钟、异步时钟组、外部输入/输出延时及 ADC 延迟两拍捕获的多周期路径。Gray 指针网络采用16.667ns最大网络延时约束，独立于异步时钟组切断，限制传播延时及总线偏斜。外部延时是当前板级预算，补齐接线后需结合接口电平、板卡走线和器件时序要求复核，并完成布局布线后的 setup/hold、CDC 和未约束路径检查。

无需参考工程即可运行全部测试。若要重新生成协议黄金向量：

```powershell
python scripts/reference_vectors.py --reference E:/houxf/afs_fireware
```

该脚本提取固定提交中 `USB_axi4lite` 和 `packet` 的原始 CRC 函数，经 Icarus 运行后生成 `sim/reference_vectors.vh`，不是用新实现自己生成期望值。

当前验证结果见 `doc/validation.md`。仿真使用 [Icarus Verilog Windows 发行版](https://bleyer.org/icarus/)；本机 Questa 已完成语法编译，但因运行许可证缺失，自动运行采用 Icarus。
