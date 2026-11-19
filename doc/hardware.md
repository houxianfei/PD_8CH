# 硬件接口和完整引脚

目标 FPGA：**Cyclone IV E EP4CE6E22C8（E22封装）**。顶层：[rtl/PD.v](../rtl/PD.v)；引脚唯一来源：[par/PD.qsf](../par/PD.qsf)；时序约束：[par/PD.sdc](../par/PD.sdc)。当前28个顶层信号均已分配，不存在待补的下层板输入管脚。

以下 `PIN_x` 是FPGA封装引脚编号，不是板卡连接器针脚号。更换板卡时应按原理图同步调整QSF和本表，不能仅凭连接器位置照搬。

## 时钟、复位和共用ADC控制

| 端口 | FPGA方向 | 引脚 | 说明 |
|---|---|---|---|
| `clk50M` | 输入 | PIN_23 | 50MHz系统时钟 |
| `usb_clk_60m` | 输入 | PIN_25 | FT232H提供的60MHz FIFO时钟 |
| `Rst_n` | 输入 | PIN_70 | 低有效复位 |
| `spics` | 输出 | PIN_99 | 上下层共用ADC片选 |
| `spiclk` | 输出 | PIN_101 | 上下层共用25MHz SPI时钟，mode 2 |
| `spido` | 输出 | PIN_104 | 上下层共用ADC配置/发送数据 |

上下层各四个输入，共四片AD7380。两层使用同一套SPI控制输出同步转换，不拆为两套T/B控制线。

## ADC输入与软件通道

| 软件通道 | 顶层输入 | 引脚 | ADC寄存器 | 上传组 |
|---|---|---|---|---|
| CH0 | `spidiT[0]` | PIN_71 | 0x00 | 0 |
| CH1 | `spidiT[1]` | PIN_73 | 0x01 | 0 |
| CH2 | `spidiT[2]` | PIN_75 | 0x02 | 0 |
| CH3 | `spidiT[3]` | PIN_76 | 0x03 | 0 |
| CH4 | `spidiB[0]` | PIN_106 | 0x04 | 1 |
| CH5 | `spidiB[1]` | PIN_105 | 0x05 | 1 |
| CH6 | `spidiB[2]` | PIN_103 | 0x06 | 1 |
| CH7 | `spidiB[3]` | PIN_100 | 0x07 | 1 |

软件中的Name只影响显示/保存名称，不改变这个映射。原始值按有符号16位ADC码解释；Power为软件系数计算结果，不改变FPGA数据。

## FT232H同步245 FIFO

| 端口 | FPGA方向 | 引脚 | 含义 |
|---|---|---|---|
| `usb_data[0]` | 双向 | PIN_144 | FIFO数据bit0 |
| `usb_data[1]` | 双向 | PIN_143 | FIFO数据bit1 |
| `usb_data[2]` | 双向 | PIN_142 | FIFO数据bit2 |
| `usb_data[3]` | 双向 | PIN_141 | FIFO数据bit3 |
| `usb_data[4]` | 双向 | PIN_138 | FIFO数据bit4 |
| `usb_data[5]` | 双向 | PIN_137 | FIFO数据bit5 |
| `usb_data[6]` | 双向 | PIN_136 | FIFO数据bit6 |
| `usb_data[7]` | 双向 | PIN_135 | FIFO数据bit7 |
| `usb_oe_n` | 输出 | PIN_125 | 低有效输出使能 |
| `usb_rd_n` | 输出 | PIN_129 | 低有效读 |
| `usb_rxf_n` | 输入 | PIN_120 | 低有效RX FIFO非空 |
| `usb_siwu_n` | 输出 | PIN_127 | 当前保持高电平 |
| `usb_wr_n` | 输出 | PIN_128 | 低有效写 |
| `usb_txe_n` | 输入 | PIN_132 | 低有效TX FIFO可写 |

FT232H通过USB连接主机，FPGA通过8bit并行FIFO连接FT232H。此USB接口用于采集，不是JTAG下载接口；配置FPGA另用USB-Blaster等JTAG下载器。软件使用D2XX，并在连接时设置同步FIFO bit mode `0x40`；不重写FT232H EEPROM。板卡仍需具备相应FIFO布线/EEPROM配置。

## 电平和时序

QSF当前默认I/O standard为 **2.5 V**，未使用管脚输入三态。该值描述现有工程配置，不表示任意FT232H/ADC板都可直接接入；换板时对照原理图、VCCIO和接口电平复核。

SDC定义20.000ns系统时钟、16.667ns FTDI时钟和25MHz ADC生成时钟，包含USB/SPI外部延时、ADC延迟两拍捕获约束，以及跨域Gray指针16.667ns最大网络延时约束。`scripts/check_timing.tcl`只检查综合网表的时钟和约束端点；完整编译的 `PD.sta.rpt` 才包含布局布线时序结果。

当前完整编译报告的setup/hold等汇总裕量为正，但Unconstrained Output Ports仍列出 `spiclk`（生成时钟所在的输出端口未设独立output-delay）。详见[验证记录](validation.md)。更换板卡或调整SPI时序时应复核这个时钟输出路径和外部预算；不要将“管脚齐全”或“Compilation Successful”解释为所有板级路径已实测。

## 检查管脚

`scripts/check_pins.tcl`检查28个必需信号均有位置约束且没有重复引脚。Quartus完整编译（含GUI Start Compilation）自动调用；也可单独执行：

```powershell
& 'C:/intelFPGA_lite/22.1std/quartus/bin64/quartus_sh.exe' -t ./scripts/check_pins.tcl
```

正常输出 `Board pin check passed.`。它检查约束完整性，不判断PCB接线是否正确。
