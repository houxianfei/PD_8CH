# PD 8CH 1.0.0 命名与版本

PD基于AFS设计基线独立开发。当前固件、软件和SDK统一使用PD，产品首版为1.0.0。真实参考仓库名、分支、提交、基线路径及第三方许可证保留原文，不执行无差别替换。

## 新入口与SDK改名

| AFS基线入口/标识 | PD入口/标识 |
|---|---|
| `par/AFS.qpf`、revision/top AFS | `par/PD.qpf`、revision/top PD |
| `rtl/AFS.v`、AFS_core | `rtl/PD.v`、PD_core |
| `software/afs_8ch.sln` | `software/pd_8ch.sln` |
| `include/afs/afs_driver.h` | `include/pd/pd_driver.h` |
| `AFS::af_driver`、`AFS::af_data` | `PD::pd_driver`、`PD::pd_data` |
| AFS命名空间下的其他公共类型/函数 | 相同类型/函数名，命名空间改为PD |
| afs_8ch.exe及相关测试/SDK演示程序 | pd_8ch.exe及对应pd_8ch前缀程序 |
| cfg/afs_8ch.ini、[AFS][UI] | cfg/pd_8ch.ini、[PD][UI] |
| AFS校准/CSV标识 | PD8CH_CALIBRATION_V2 / PD8CH_REALTIME_CSV_V2 |

SDK不保留旧命名空间、旧头文件或类型别名。示例：

```cpp
#include "pd/pd_driver.h"
PD::pd_driver sdk(PD::DriverMode::Simulation);
sdk.search_devices();
```

公开API仍是异步请求，操作顺序和等待方式见[SDK演示](../software/docs/simulation.md)。ADC八通道顺序、校准公式及原有上传帧/CRC不变；版本号使用新增寄存器，不改变采样帧。

## 配置与安装独立

PD不读取旧AFS ini、AppData或校准文件，也不自动迁移名称/系数。首次运行使用默认值；后续自动恢复自己的pd_8ch.ini。只接受带PD8CH_CALIBRATION_V2头的校准文件；手动选择旧文件时返回英文错误，保留当前参数。

安装AppId、目录和快捷方式独立于AFS，可并存。旧产品文件、个人配置及历史build/publish产物不被改名、删除或覆盖。发布时仅复制默认cfg，不包含开发者的个人配置。新构建目录为 `software/build/PD_8CH_v1.0.0/`，安装/免安装包使用PD_8CH前缀。

## 固件版本与Main UI

连接后Main UI显示Software version和Firmware version。软件版本来自产品构建资源；固件版本从设备读取，两者不要求数字一致，不做相等性兼容判定。

新增只读寄存器 **0xF4**，内部AXI地址 **0x400003D0**，格式为 `{reserved[7:0], major[7:0], minor[7:0], patch[7:0]}`。PD固件1.0.0返回 **0x00010000**；写入忽略，复位不改变版本。

读命令：`AA 02 F4 00 00 00 00 B2`。1.0.0应答：`00 00 01 00 00 00 01 00`，即32位小端值重复两次。

SDK在连接准备/排空后、发布Connected前读取一次，Snapshot提供 `has_firmware_version` 和 `firmware_version`，`format_firmware_version()`负责显示。采集中仅用缓存，不读取寄存器。断开/故障清除当前版本，重连重新读取。零值或保留字节非零显示Unknown，仍允许连接；查询超时或应答错误则连接失败并释放资源。未连接显示Not connected，Simulation显示1.0.0并标记simulated。

固件版本在 `rtl/control_register.v` 的FIRMWARE_VERSION维护；模拟版本在公共SDK头文件的SIMULATION_FIRMWARE_VERSION维护。软件版本在app_version.h、资源和安装/构建配置中维护。修改版本时同步更新测试、文档和发布产物。
