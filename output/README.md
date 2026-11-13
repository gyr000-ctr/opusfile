# 增益统一（loudness normalization）改动与验收报告

## 需求与实现

素材响度差异大，播放前按增益统一。在 libopusfile 中实现：

1. **读取并应用增益**：沿用并整理现有机制 —— 解码时自动应用 OpusHead
   `output_gain`，`op_set_gain_offset()` 可选择 HEADER/ALBUM/TRACK/ABSOLUTE
   增益源（含 R128_ALBUM_GAIN / R128_TRACK_GAIN 标签）。
2. **应用开关（新增）**：`op_set_gain_enabled(of, 0)` 关闭增益应用，解码器
   被设置为 0 增益，输出样本与未加增益的原始解码**逐一完全一致**；
   `op_gain_enabled()` 查询开关状态。默认开启，行为与改动前一致。
3. **越界钳制并报告（新增报告路径）**：总增益钳制到 [-32768, 32767] Q8 dB
   （即 [-128, 128) dB）。新增 `op_get_gain(of, &clamped)` 返回生效增益，
   并通过 `clamped` 输出参数报告是否发生了钳制。
4. **与解码路径共用输出缓冲**：增益通过 `OPUS_SET_GAIN` 编程进 libopus
   解码器，在填充调用方输出缓冲时原地生效 —— 没有新增任何缓冲或二次
   全量拷贝（`output/logs/op_update_gain.c` 为证）。

## 改动清单（多处小改 + 公共层，未复制现有路径）

| 文件 | 改动 |
|---|---|
| `src/internal.h` | `OggOpusFile` 增加 `gain_enabled` 字段；声明公共层 `op_gain_compute()` / `op_gain_clamp()` |
| `src/internal.c` | 实现公共层：增益合成（从 `op_update_gain` 抽出，解码路径与报告 API 共用）与钳制+报告 |
| `src/opusfile.c` | `op_update_gain()` 改用公共层并支持开关；`op_open1()` 默认 `gain_enabled=1`；新增 `op_set_gain_enabled()` / `op_gain_enabled()` / `op_get_gain()` |
| `include/opusfile.h` | 三个新公开 API 的声明与文档 |
| `examples/opusfile_example.c` | 仓库自带入口增加 `--no-gain` / `--gain-offset=<q8>` 选项，并在打开后打印生效增益与钳制报告 |

## 验证（仓库自带入口 `opusfile_example`）

脚本：`output/tools/verify.sh`；完整日志：`output/logs/verify.log`。

1. **增益后能量对齐**：同一信号以 -10/-20/-30 dBFS 三版响度编码，头部分别
   写入 -6/+4/+14 dB 增益；解码输出 RMS 分别为 -19.019 / -19.013 /
   -19.008 dBFS，离散度 0.011 dB（阈值 0.2 dB）→ **对齐**。
   关闭增益后 RMS 为 -13.0 / -23.0 / -33.0 dBFS，原始 20 dB 响度差保留。
2. **关闭时零差异**：`--no-gain` 解码 +4 dB 文件 vs 正常解码同流零增益
   副本，`cmp` 完全一致（两路径都以增益 0 编程解码器）。
3. **越界钳制并报告**：`--gain-offset=98303` → 生效 32767 Q8（128.00 dB）
   并打印 clamped 报告；`-98302` → -32768 Q8（-128.00 dB）；范围内
   `2560`（10 dB）不误报。
4. **无额外拷贝**：静态检查确认增益路径（`op_update_gain` 及新 API）不含
   任何 memcpy/内存分配；增益在解码填充输出缓冲时原地生效。

## 产物

- `output/demo.mp4` / `output/demo.gif` / `output/demo.cast` —— 分段录屏
  （5 段，段首标题卡、段间停顿，全程无键盘交互，asciinema 录制后渲染）
- `output/logs/verify.log` —— 验收日志；`output/logs/op_update_gain.c` —— 增益路径代码取证
- `output/data/` —— 测试素材（不同响度/增益的 .opus）与解码输出 .wav
- `output/tools/` —— 验证与录制脚本（verify.sh、record_demo.sh、
  gen_source.py、patch_gain.py、wav_rms.py、cast_to_gif.py）
