# 验证记录

日期：2026-11-19。参考：`afs_v1`，提交 `ae1a285`。

## 自动仿真

执行 `./scripts/test.ps1`，以下测试全部通过；原始输出保存在 `sim/test.log`。

| 测试 | 验证内容 | 结果 |
|---|---|---|
| `tb_adc` | mode2 SPI 的16个数据时钟、初始化字 A000/9282、8通道数据顺序、50拍采样间隔、同时锁存、初始化期间无 valid、复位后重新配置 | PASS |
| `tb_register` | AXI AW/W 分别先到、WSTRB 字节选通、读/写响应在背压期间保持、只读/非法/未对齐地址、控制位屏蔽、完成与清空标志、异步复位 | PASS |
| `tb_core` | 读写、错误CRC丢弃、黄金命令、分段/跨命令拼接、单组/双组/仅帧头、有限帧数、连续启停、上传中读寄存器、随机背压、FIFO满暂停、清空与重启 | PASS |
| `tb_ft232h` | 完整 AFS 顶层与参考 FTDI 内核，上下层板各4通道输入及共用 SPI 控制输出、独立50/60MHz时钟、FT232H 8bit双向总线、随机TXE背压、USB拆包、读应答和采样数据逐字节对照黄金向量、复位恢复 | PASS |

ADC 测试仅缩短上电等待计数，配置阶段保留2048拍；正常采样的时钟和 SPI 分频未缩短。全部为数字逻辑仿真，不代表模拟输入精度或实板电气时序已验证。

`sim/reference_vectors.vh` 由 `scripts/reference_vectors.py` 调用固定提交的原始 RTL CRC 函数生成。FTDI 测试逐字节验证原协议读应答和双组上传头/载荷，包含完整帧 CRC。12个 FTDI 内核源文件逐个与指定提交核对，除换行格式外完全相同；其 SHA256 清单见 `reference_manifest.json`。

本机 Questa Intel Starter 2021.2 可执行 Verilog 语法编译，但 `vsim` 报运行许可证不可用；上述动态仿真均使用 Icarus 完成。

## Quartus 综合

执行 `./scripts/build.ps1 -Mode synth`：**成功，0 errors，6 warnings**。

工具：Quartus Prime Lite `22.1std.1 Build 917`；器件：`EP4CE6E22C8`。

| 资源 | 综合估算 |
|---|---:|
| Total logic elements | 3,145 |
| Dedicated logic registers | 2,329 |
| Total memory bits | 62,720 |
| Embedded multipliers | 0 |
| PLL | 0 |

采样 FIFO、TX/RX 异步 FIFO 和 FTDI 内核的延迟提交 FIFO 均推断为 `altsyncram`。无未定义模块、无推断锁存器、无源文件/引脚名称失配、无忽略的 SDC 约束告警。

剩余告警为：采样 FIFO 推断加入读写穿透逻辑；两个双时钟 RAM 的读写同址行为提醒；参考 FTDI 内核未连接的只读调试输出；以及 `usb_siwu_n` 固定高电平。FIFO 指针逻辑不消费空状态下的 RAM 输出，不依赖有效事务的同址读写结果。没有通过全局屏蔽告警处理这些信息。

原始报告：`par/synth.log`、`par/output_files/AFS.map.rpt` 和 `AFS.map.summary`。

## 时序约束与构建检查

执行 `./scripts/build.ps1 -Mode timing`：**成功，0 errors，0 warnings**。

- 50MHz、60MHz 和 ADC 25MHz 生成时钟均存在。
- ADC 数据捕获寄存器128个，约束端点可解析。
- 两个异步 FIFO 的每组 Gray 指针源/目的寄存器各18个。
- 最大网络延时报告含36条指针网络，要求16.667ns，约束未因异步时钟分组失效。

这是基于综合网表的约束检查；报告中的网络延时为综合阶段估算，**不属于布局布线后的 setup/hold、Fmax 或 CDC 物理收敛结论**。时钟不确定度推导也需在最终布局布线网表再次检查。

执行 `./scripts/build.ps1 -Mode compile`，Quartus 在 `PRE_FLOW_SCRIPT_FILE` 阶段按预期阻止正式编译，明确报告缺少下层板 `spidiB[0]`～`spidiB[3]` 的位置约束。上层板 `spidiT[0]`～`spidiT[3]` 保持原四通道的实际管脚顺序。没有进入正式布局布线或生成烧录文件。检查输出见 `par/compile_guard.log`。

补齐下层板四根输入位置、确认 I/O Bank 电平并复核外部时序预算后，需继续正式编译、时序收敛和上板采集/USB验证。
