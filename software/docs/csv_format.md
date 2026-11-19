# realtime_measure CSV（v1.4.0）

保存按钮为 **Save selected channels (CSV)**。勾选哪些通道，就导出哪些通道；每个通道保留 ADC 和 Power 两列。USB 仍采集八通道，内部缓存也保留八通道，因此可改变勾选后重新导出，不必重新采集。未选任何通道时保存按钮禁用。

## 文件内容

文件为 UTF-8（带 BOM），字段中的逗号、引号按 CSV 规则转义。开头以 `#` 标记的记录为元数据，后面是数据列名和样本。示例选择 CH1、CH3：

```csv
# acquisition_start_utc,2026-11-19T00:00:00.123456Z
# acquisition_end_utc,2026-11-19T00:00:00.143456Z
# data_start_utc,2026-11-19T00:00:00.123456Z
# data_end_utc,2026-11-19T00:00:00.133456Z
# calibration_formula,Power = P1 * (P0 + ADC)
# channel,CH1,"Probe A",P0,2,P1,3
# channel,CH3,"Probe B",P0,0.5,P1,-2
timestamp_utc,"CH1 [Probe A] ADC","CH1 [Probe A] Power","CH3 [Probe B] ADC","CH3 [Probe B] Power"
2026-11-19T00:00:00.123456Z,10,36,-5,9
```

通道编号固定，自定义名称同时写入列名和校准元数据；重名也不会合并列。名称为空时回退显示 CH 编号。选择两个通道时一共5列（时间戳加两组 ADC/Power），选择八个通道时为17列。

## 时间与采集信息

- `acquisition_start_utc`：主机开始下发上传启动命令时记录的 UTC 时间。
- `acquisition_end_utc`：停止、有限采集完成或采集异常时的主机 UTC 时间。采集中导出时此项为空，采集状态会标记为 Collecting。
- `data_start_utc` / `data_end_utc`：本文件实际保留样本的起止时间，始终填写。缓存覆盖了最早样本时，data_start 会晚于 acquisition_start。
- `snapshot_at_utc`：取得本次导出缓存快照的时间；`exported_at_utc`：后台写文件开始时间。
- 数据行的 `timestamp_utc`：`acquisition_start_utc + FPGA相对时间戳`，为 ISO 8601 UTC 格式，精确显示至微秒并带 `Z`。上海时间为 UTC+08:00。

这是主机启动时间与 FPGA 相对计时的对应关系，没有额外的硬件 UTC 授时。USB 命令传输延迟会影响绝对时间锚点的准确性，六位小数不表示硬件授时精度；采集中电脑时间校正不会改变已建立的样本时间轴。

文件还记录：软件版本、设备序列号、上传频率/间隔、连续或有限模式、设置时长/帧数、选中通道数、导出样本数、累计接收/覆盖数、CRC错误、时间戳跳号和采集警告。

`data_source=hardware` 表示硬件采集；`data_source=simulation` 表示仿真信号。仿真同样使用主机UTC锚点加模拟采样时间，`timestamp_source` 标记为 `host_start_command_plus_simulated_relative_timestamp`。该标记取自缓存所属的采集模式，不受导出后切换界面模式影响。

## 校准与保存快照

`# channel` 记录本次导出所选通道的名称、P0、P1，`calibration_file` 记录界面当前的校准文件路径，`calibration_applied_at=export` 表示使用导出时的界面系数重新计算全部保留 ADC 的 Power。校准文件尚未另存时，也会在 CSV 中记录实际使用的系数。

点击保存并确认文件名后，SDK一次取得完整保留缓存及其采集时间信息，同时固定当前选择、名称和校准系数；后台写入时继续采集或修改系数，不会改变正在写出的文件。只按 UI 最近1500点生成的轻量快照不能直接导出，防止误保存为不完整缓存。

## 读取与测试

在 Python/pandas 中可使用 `read_csv(path, comment='#', encoding='utf-8-sig')` 读取数据表；元数据可另外按 CSV 记录读取。完整示例见 `example_selected_channels.csv`，其中为固定测试数据，非板卡采集结果。

```powershell
./software/scripts/build.ps1 -Configuration All -Test
python ./software/scripts/verify_csv.py ./software/build/logs/csv_Debug
python ./software/scripts/verify_csv.py ./software/build/logs/csv_Release
```

验证覆盖单选、双选、全选、名称中的逗号/引号、UTC绝对时间、跳号、缓存覆盖后的数据范围、采集中保存及校准值。旧 SDK 原始九列/十七列导出函数仅用于兼容调用，GUI 不再使用这些相对时间格式。
