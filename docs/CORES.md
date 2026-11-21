# 核心报告：按 game nds 的标准做的全核心优化

本文件记录 2026-11-21 这一轮“所有核心按 NDS 核心的标准处理”的完整证据。
原则只有一条：**引擎来自上游，不做重写；gameOS 做的事是读源码找热路径、
按真实运行剖面构建、并用同一 ROM 前后哈希证明输出不变。** 数字全部来自
主机测试台，掌机实测另见 [PLATFORMS.md](PLATFORMS.md) 的状态栏。

## 方法

- 测试台：`host-test/ndsrun`（aarch64 静态链接）在
  `qemu-aarch64 -L buildroot/output/target` 下运行真实 libretro 核心；
  固定按键脚本、固定 1800 帧，`GAMEOS_BENCH_HASH=1` 打开 FNV 视频/音频哈希。
- 基线：同一配方但不带 PGO 的干净构建；优化版：`pgo/` 剖面计数 +
  `-fprofile-use -fprofile-correction`。两边其余标志完全相同。
- 重复：每个核心 3 次 × 1800 帧，取平均；哈希必须逐字节一致。
- 剖面生成：在 Buildroot 最终产物路径上做插桩构建（`-fprofile-generate`），
  用真实 ROM 跑 1800–3600 帧。剖面与编译器、优化级别、`-D` 宏全部绑定，
  更换任何一项都必须重新生成，否则 `-Werror=coverage-mismatch` 会拒绝构建。

## 总表（主机测试台，帧/秒）

| 核心 | 平台 | 基线 | 优化版 | 收益 | 哈希 |
|---|---|---|---|---|---|
| melonDS 0.9.3 + gameOS JIT | NDS | 74.5 | 121.3 | ×1.63 | 一致 |
| Snes9x `204ea1c` | SNES | 292 | 342 | +17% | 一致 |
| Genesis Plus GX `4fd32ef` | MD | 522 | 633 | +21% | 一致 |
| mGBA 0.10.5（GBA） | GBA 备选 | 348 | 369 | +6%（单独测试最好 +8%） | 一致 |
| mGBA 0.10.5（GB） | GB/GBC | 821 | 828 | ~+1% | 一致 |
| gpSP | GBA 默认 | 1395 | 1450 | +4% | 一致 |
| FCEUmm `77468e5` | NES | 975 | 968 | 持平（运行噪声） | 一致 |
| PCSX-ReARMed | PS1 | 1936 | 1933 | 0（有意） | — |

上表为最终一次统一扫测（同一会话、每核心 3×1800 帧均值）；单独隔离
测试中 Snes9x 曾测到 +16.4%、Genesis +24.6%、gpSP +1.9%、FCEUmm +2.1%，
与均值差落在运行噪声内，故对外文档取保守口径。

NDS 的具体补丁（JIT 块链接、标志缓存、16 字节失效、按需内存映射、
线程化渲染）见 ARCHITECTURE.md 与 1.0.0 发布说明；其余核心记录如下。

## Snes9x（SNES）

- 源码研读：C++ 引擎，热点集中在 `ppu.cpp`（逐层/逐 tile 合成与
  调色板转换）和 `cpu.cpp`（65816 解释循环），音频 `apu/` 占比较小。
- 做法：剖面用 gothicvania.sfc（动作场景，PPU/CPU 同时吃紧）跑 1800 帧；
  配方把 `-fprofile-use` 追加进 `CFLAGS/CXXFLAGS`，构建**不启用 LTO**
  （上游明确说明 LTO 会错误编译该核心）。
- 结果：292 → 342 fps（+17%），哈希一致。该核心构建在 `libretro/`
  子目录，剖面的对象路径与正式构建一致。

## Genesis Plus GX（Mega Drive）

- 源码研读：68K 与 Z80 解释器 + VDP 渲染是主要开销；`HAVE_CDROM=0`
  （掌机无光驱，也避免引入 SCSI ioctl）。
- 做法：剖面用 battle-4tris.bin 跑 1800 帧；在默认 `-O2` 下生成并用
  `-O2` 使用——其捆绑的 LZMA 源码在 `-O3` 下会触发
  `-Werror=coverage-mismatch`，因此不高抬优化级别。
- 结果：522 → 633 fps（+21%），哈希一致。这是本轮收益最大的核心。

## FCEUmm（NES）

- 源码研读：纯 C 引擎，热点在 PPU 逐扫描线渲染与 6502 解释循环；
  `-O3` 对聚合循环帮助有限。
- 做法：剖面用 240pee.nes（内置 PPU 测试场景）跑 1800 帧。
- 结果：975 → 968 fps（持平，三次运行 938–990，属 qemu 运行噪声），
  哈希一致。收益不显著；NES 本身在这颗芯片上余量极大（远高于 60 fps），
  没有继续深挖。保留 PGO 构建是因为哈希证明无副作用，且产物更小。

## mGBA（GB / GBC，GBA 备选）

- 源码研读：0.10.5 的 CMake 构建，`src/platform/libretro` 没有 Makefile；
  `LIBMGBA_ONLY=ON` 构建无外部依赖的最小静态核心，`BUILD_LIBRETRO=ON`
  产出 `mgba_libretro.so`。解释器核心（无 JIT），热点在 ARM/GB CPU 解释
  与 PPU 合成。mGBA 自带的 `BUILD_PGO` 在 `LIBMGBA_ONLY` 下不可用，
  因此标志直接放进 `CMAKE_{C,CXX}_FLAGS`。
- 做法：剖面按 GB 加权——3600 帧 gb240p.gb + 1800 帧 gbatris.gba，
  GB 是本机默认，GBA 用 gpSP、mGBA 作备选。
- 结果：GB 821 → 828 fps（约 +1%），GBA 348 → 369 fps（+6%，单独测试
  最好一次 +8%），哈希一致。GB 已经远超 60 fps，瓶颈不在这；GBA 的提升
  来自解释循环分支布局优化。

## gpSP（GBA 默认核心）

- 源码研读：ARM64 动态编译器。真正执行的热代码是运行时生成的块，
  编译期剖面只能覆盖 dynarec 自身、解释回退、内存/IO 派发和 GPU 行渲染
  这些“外壳”。
- 做法：剖面用 gbatris.gba 1800 帧；gpSP 的 Makefile 会重置 `LDFLAGS`，
  生成阶段必须把链接标志显式放到 make 命令行，使用阶段则由配方统一传入。
- 结果：1395 → 1450 fps（+4%），哈希一致。收益有限的原因与 PCSX
  相同（热点在运行时生成的代码里），但外壳仍值得优化。

## PCSX-ReARMed（PS1）：有意不做

- 热点是 ari64 dynarec 运行时生成的 MIPS 翻译代码，编译期剖面完全够不着。
- 实测：PGO 构建 +1%（3×1800 帧 nortis，属噪声）；`-O3` 在 nortis 与
  240p 套件上测得与 `-O2` 相同。
- 决定：保持上游默认（neon GPU、ari64、异步 GPU），不引入无收益的
  构建差异。

## PPSSPP（PSP）

- 独立进程，不通过 libretro；引擎未改。性能要点（1 倍渲染分辨率、
  跳过 GPU→CPU 回读）已在 1.0.0 完成，见 README 性能表。
- 启动修复：PPSSPP 链接的四个自带辅助库（cpu_features、imgui、
  glslang、SPIRV）以前没有装进镜像，动态加载器会在 main() 之前退出，
  PSP 游戏实际无法启动；现已随包安装并由 `verify-image.sh` 检查。
  PSP 的启动预读（程序、库、UI 图集、VFPU 表、ISO 前 16 MB）也接入了
  统一的启动路径。

## 复现

```sh
# 主机 A/B（需已构建对应核心目录）
sh host-test/bench.sh buildroot/output/target/usr/lib/libretro /tmp/bench.tsv 1800 3

# 仅重建某个核心（使用配方内置剖面）
make gameos-core-snes9x-rebuild
```

重建剖面（升级上游或改编译标志后必须做）：
用 `-fprofile-generate=<abs-dir>`（与正式构建相同的 PATH 与 TARGET 标志）
走一遍 Buildroot 构建，`qemu-aarch64` 下用测试台跑代表 ROM，再把 `.gcda`
收进 `buildroot/package/<pkg>/pgo/`。剖面文件路径编码了绝对构建目录，
因此必须在最终 Buildroot 输出路径上生成。

## 边界

- 以上全部是电脑上的 qemu 数据，**不是掌机帧率**。掌机上的帧时间、
  温度、功耗仍未测（`NOT RUN`）。
- 哈希一致只能证明“同 ROM 前 N 帧的输出没变”，不能证明所有游戏都无误。
- 主机 fps 远超 60 不代表掌机同样富余；Mali-G52 上的实际渲染开销
  只有真机能给出答案。
