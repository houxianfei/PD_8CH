# 固件验证记录

复核日期：2026-11-19。目标：Cyclone IV E EP4CE6E22C8；工具：Quartus Prime Lite 22.1std.1 Build 917。参考内核：afs_v1固定提交ae1a285。

## 当前状态

**28个顶层信号已全部分配实际管脚，完整编译成功并生成PD.sof。** 早期“spidiB[0..3]缺管脚、仅可综合、正式编译被阻止”的说明已过期，不再适用于当前QSF。实际对应关系见[硬件接口](hardware.md)。

| 检查 | 本次结果 | 复现入口 |
|---|---|---|
| RTL自动仿真 | 四个testbench均PASS | `scripts/test.ps1` |
| 管脚完整性及唯一性 | PASS，28个信号，无缺失/重复 | `scripts/check_pins.tcl` |
| Quartus完整编译 | Successful，0 errors、8 warnings | `scripts/build.ps1 -Mode compile` |
| SOF | 已生成，358695字节 | `par/output_files/PD.sof` |
| SDC/CDC端点脚本 | PASS；完整编译后执行有1条-post_map忽略提示 | `scripts/build.ps1 -Mode timing` |
| 实板下载/八路输入/USB长期采集 | 本次文档复核未执行 | 按[入门指南](getting_started.md)另行记录 |

编译产物和原始日志由.gitignore排除，新clone不会携带。请在本机构建并查看生成报告；表内数字是本次运行结果，不是其他工具版本/修改后工程的固定保证。

另用 `git clone --no-hardlinks` 创建无本机build/.tools产物的独立工作副本：Release构建、全部C++测试、六步SDK演示以及四个RTL仿真均通过。工具链由外部安装目录提供，源码和黄金向量均来自克隆副本，未使用原始GUI/固件参考仓库。这验证了首次clone的源码依赖完整性；未在干净副本重复安装系统驱动或下载板卡。

## 自动仿真

使用Icarus Verilog 12.0执行，四项全部通过：

| 测试 | 验证内容 |
|---|---|
| `tb_adc` | mode2 SPI的16个数据时钟、初始化A000/9282、八通道顺序、50拍采样间隔、同时锁存、初始化无valid、复位后重新配置 |
| `tb_register` | AXI AW/W分别先到、WSTRB、背压时响应稳定、只读/非法/未对齐地址、控制位屏蔽、完成/清空标志、异步复位 |
| `tb_core` | 错误CRC、黄金命令、拆包/拼接、单组/双组/仅帧头、有限/连续启停、背压、FIFO满暂停、清空重启 |
| `tb_ft232h` | 完整顶层、共用SPI输出、独立50/60MHz时钟、8bit双向FIFO、TXE背压、命令拆包、应答/采样逐字节黄金向量对照、复位 |

ADC测试缩短上电等待，保留2048拍配置阶段；正常转换/SPI分频未缩短。它验证数字逻辑，不测模拟输入精度或真实板级时序。

`sim/reference_vectors.vh`已提交，普通测试不依赖原参考仓库。重新生成时脚本提取固定提交的真实RTL CRC函数。FTDI内核参考文件清单及LF归一化SHA256见[reference_manifest.json](reference_manifest.json)。历史Questa环境运行许可证不足，因此自动流程使用Icarus；这不阻止本项目当前自动仿真。

## 完整编译与资源

本次执行 `./scripts/build.ps1 -Mode compile`，依次完成管脚检查、综合、Fitter、Assembler和Timing Analyzer。

| 指标 | 综合结果 | Fitter结果 |
|---|---:|---:|
| Logic elements | 3145 | 2871 / 6272（46%） |
| Dedicated logic registers | 2327 | 2319 / 6272（37%） |
| Memory bits | 62720 | 62720 / 276480（23%） |
| I/O pins | 28 | 28 / 92 |
| Embedded multipliers | 0 | 0 |
| PLL | 0 | 0 |

生成 `par/output_files/PD.sof`。报告位于同目录 `PD.flow.rpt`、`PD.map.summary`、`PD.fit.summary`、`PD.fit.rpt`、`PD.pin`、`PD.sta.summary`和`PD.sta.rpt`。不要把早期综合估算数与Fitter实际占用混为一项。

完整编译的8条warning包含以下类别：RAM读写穿透/双时钟同址行为、参考内核连接提示、usb_siwu_n常量输出、Lite环境LogicLock许可提示，以及部分输出未单独设置drive strength/slew rate。最后一类属于I/O电气属性，不是缺少LOCATION引脚约束；管脚位置已经完整。未通过屏蔽告警来取得成功状态。

## 时序结果及范围

以下取自本次完整布局布线 `PD.sta.summary` 的Slow 1200mV 85C模型，单位ns，TNS均为0：

| 时钟 | Setup slack | Hold slack |
|---|---:|---:|
| ftdi_clk（60MHz） | 0.663 | 0.361 |
| sys_clk（50MHz） | 7.159 | 0.445 |
| adc_sclk（25MHz） | 10.348 | 4.128 |

同一summary包含Slow 0C和Fast 0C结果，setup/hold/recovery/removal/minimum pulse width汇总均为正。这里的正裕量只覆盖已约束/已报告的路径。

**已知约束范围：** `PD.sta.rpt`的Unconstrained Paths Summary仍列出1个输出端口 `spiclk`，未设独立output-delay；Unconstrained Clocks和Input Ports为0。报告提示设计未完全约束setup/hold。当前没有把此项解释成所有板级时序已签核，也没有在文档任务中改动SDC或电气参数。换板或继续板级时序收敛时，应复核SPI时钟输出路径、VCCIO、驱动强度和外部延时预算。

`check_timing.tcl`检查结果：sys_clk、ftdi_clk、adc_sclk均存在，ADC捕获寄存器128个，每类Gray指针源/目的寄存器18个；最大网络延时约束可解析。脚本请求-post_map：在干净综合之后使用综合网表；已经运行Fitter时，Quartus会忽略-post_map并使用可用网表，给出332199提示。本次是在完整编译后执行，端点检查通过，0 errors、1 warning。该脚本不是单独的完整时序签核流程。

## 实板与软件验证

本次没有操作板卡下载、Flash编程、FT232H EEPROM、实际模拟输入或系统驱动安装。SOF生成、数字仿真和已约束路径的STA结果不能代替真实输入、USB枚举、频率测量、长时稳定性及拔插恢复记录。

软件当前版本v1.0.0，已完成Debug/Release测试、无硬件SDK例程、CSV独立解析及离屏UI验证，具体边界见[软件验证记录](../software/docs/validation.md)。整机联调建议按[首次使用指南](getting_started.md)记录提交号、板卡/设备序列号、配置和CSV，作为后续实测证据。

## PD更名与版本寄存器复核

顶层/核心改为PD/PD_core，28个LOCATION和全部SDC约束与原工程一致；除新增F4只读版本外，采集和协议逻辑不变。四项RTL测试、完整PD工程编译及端点检查均通过，生成PD.sof。新增版本测试覆盖写保护和复位后的稳定读取；命令和上传黄金向量内容未变。资源/时序表已更新为此次PD构建结果，spiclk未约束输出项仍保留如实记录。
