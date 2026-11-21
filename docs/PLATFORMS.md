# 平台核心报告（NES / SNES / MD）

本文件对应路线图 PR-19，记录 2026-11-21 新增三个平台的真实状态。
**状态只有两种：已验证（写出证据）与未验证（写明未做）。** 没有真机数据
的项目一律标 `NOT RUN`，不用电脑上的推算冒充掌机结果。

## 结论先行

- 三套核心均为**上游成熟核心按固定提交引入**，gameOS 的处理方式与
  `game nds` 一致：上游源码 + 构建调优 + 按真实运行剖面做 PGO，
  不重写模拟器引擎。
- 已验证：aarch64 交叉编译通过、安装进 rootfs、导出标准 libretro ABI、
  前端能扫描对应扩展名、启动路径写入正确的核心与配置文件。
- 已验证（主机层）：三套核心在同一测试台跑真实 ROM，PGO 构建相对默认
  构建的帧率收益为 Snes9x +17%、Genesis Plus GX +21%、FCEUmm 持平，
  前后视频/音频哈希完全一致（方法见下文“PGO 构建与实测”）。
- 未验证：真机启动任何一款游戏、帧时间、温度、功耗、音频、存档。
  掌机写卡目前被 p4 清零问题阻塞，本轮不刷卡。

## 总览

| 项目 | NES | SNES | Mega Drive |
|---|---|---|---|
| 前端 id | `nes` | `snes` | `md` |
| 上游仓库 | libretro/libretro-fceumm | libretro/snes9x | libretro/Genesis-Plus-GX |
| 固定提交 | `77468e568d9fe9c48d684ca911f1a3fe7259fdf7` | `204ea1c0b851f960cbf223075eb1c25641cb483c` | `4fd32efe1fec3537d11f7aaa24cb961c6ff57fdd` |
| 许可证 | GPL-2.0+ | Snes9x（非商业） | Genesis-Plus-GX（非商业） |
| Buildroot 包 | `gameos-core-fceumm` | `gameos-core-snes9x` | `gameos-core-genesis-plus-gx` |
| 产物 | `fceumm_libretro.so` | `snes9x_libretro.so` | `genesis_plus_gx_libretro.so` |
| 产物大小（实测，PGO 构建） | 4,661,264 B | 2,588,264 B | 12,588,408 B |
| 运行时依赖（`readelf -d`） | libm、libc、ld-linux | libstdc++、libm、libgcc_s、libc、ld-linux | libm、libc、ld-linux |
| RetroArch 配置 | `nes.cfg` | `snes.cfg` | `md.cfg` |
| 扫描扩展名 | `.nes .fds .unf .unif` | `.sfc .smc .fig .swc .bs` | `.md .gen .bin .smd` |

三个产物合计 19,837,936 B（约 18.9 MiB，未压缩）。这是 `output/target`
中三个 `.so` 的直接测量值；整份镜像相对上一版增加多少**未测**，因为本轮
没有做完整镜像对比。

三套构建都通过 Buildroot 2026.08 外部树、`aarch64-gameos-linux-gnu`
工具链完成，编译时显式传入 `CC/CXX/CFLAGS/CXXFLAGS/LDFLAGS`。Snes9x 在
`libretro/` 子目录构建，且**不启用 LTO**（上游明确说明该核心曾被 LTO
错误编译）；Genesis Plus GX 使用 `Makefile.libretro`，并传
`HAVE_CDROM=0`（掌机没有光驱，避免引入无用的 SCSI CD-ROM ioctl）。

## 已验证项

| 检查 | 方法 | 结果 |
|---|---|---|
| 交叉编译 | Buildroot 包构建 | 三个核心均构建成功 |
| 目标架构 | `file` / ELF 头 | AArch64 共享对象 |
| libretro ABI | `nm -D` 查 `retro_init` / `retro_api_version` / `retro_run` | 三个核心均导出 |
| 安装位置 | `output/target/usr/lib/libretro/` | 三个 `.so` 均存在 |
| 前端注册 | `tests/shell/test-platforms.sh` | 74 项通过 |
| 启动映射 | 静态检查 `launch.cpp` 与注册表 | 写入对应核心名和 `*.cfg` |
| 扫描与去重 | 主机测试：嵌套/扁平目录、`.txt` 诱饵、`.md`+`.bin` 同名校验 | 通过 |
| 主机性能 A/B | `host-test/bench.sh`，同一 ROM、同一测试台 | 见下节“PGO 构建与实测” |
| 画面/声音一致性 | 同 ROM 优化前后 FNV 视频/音频哈希 | 三核心均完全一致 |

`test-platforms.sh` 还校验了 8 个平台 id 唯一、每个平台的扩展名互不重复、
PSP 仍走独立进程。UI 布局在 6 种语言下用无头 SDL 截图工具渲染检查，
新增的 8 格平台宫格、轮播与俄语两行标签没有越界或重叠（截图不进入仓库）。

## PGO 构建与实测

三个核心与 gpSP / mGBA / melonDS 使用同一套“按真实运行剖面优化”的流程：

1. 用 Buildroot 配方在**最终产物路径**上做插桩构建
   （`-fprofile-generate=<dir>`，标志与正式构建完全一致）；
2. 在 `qemu-aarch64` 下用无头测试台 `host-test/ndsrun` 载入真实 ROM 跑
   1800 帧（NES 用 240pee.nes，SNES 用 gothicvania.sfc，MD 用
   battle-4tris.bin），生成 `.gcda` 计数；
3. 计数收进各包 `pgo/` 目录，正式构建以
   `-fprofile-use=<pkg>/pgo -fprofile-correction` 编译；
4. 用同一 ROM、同一输入脚本对比优化前后的 `host_fps`，并要求视频/音频
   哈希逐字节一致。

| 核心 | 优化前 fps | 优化后 fps | 收益 | 哈希一致 |
|---|---|---|---|---|
| Snes9x | 292 | 342 | +17% | 是 |
| Genesis Plus GX | 522 | 633 | +21% | 是 |
| FCEUmm | 975 | 968 | 持平（运行噪声） | 是 |

（主机测试台 3×1800 帧均值。这是电脑上的 qemu 数据，**不能替代掌机实测**；
掌机帧率、温度、功耗仍为 `NOT RUN`。）FCEUmm 保留 PGO 构建是因为哈希
证明输出无变化、产物更小；收益本身不显著。

剖面计数与源码/编译标志绑定：升级上游提交、改用不同优化级别或编译器时，
必须按上述流程重新生成，否则构建会因 `-Werror=coverage-mismatch` 拒绝
使用旧计数。Genesis Plus GX 的计数在默认 `-O2` 下生成（其捆绑的 LZMA
源码在 `-O3` 下会报计数不匹配），因此配方保持上游默认优化级别。

## 未验证项（真机）

| 项目 | 状态 | 说明 |
|---|---|---|
| 代表性游戏启动 | `NOT RUN` | 计划用 NES《超级马力欧兄弟 3》、SNES《超级马力欧世界》、MD《索尼克 2》 |
| 帧时间 / 掉帧 | `NOT RUN` | 没有掌机测量；电脑数据不能替代 |
| 温度 | `NOT RUN` | 需在真机跑 30 分钟后读 sysfs |
| 功耗 / 续航 | `NOT RUN` | 需电流表或电池放电测试 |
| 音频 | `NOT RUN` | 采样率、爆音、延迟均未听测 |
| 存档 | `NOT RUN` | 见下方兼容性说明 |
| 休眠 / 唤醒 | `NOT RUN` | 三核心在合盖恢复后的行为未测 |
| 完整镜像 | `NOT RUN` | 本轮未构建可刷写镜像 |

## 存档兼容性

- 三类核心都使用 libretro 标准 SRAM 接口：游戏内电池存档写入
  RetroArch 的 `.srm`，路径随 ROM 内容路径确定，可与其它使用同名核心的
  RetroArch 安装互通（未实测）。
- 即时存档（`.state`）与核心版本、编译选项绑定；跨核心（例如 Snes9x ↔
  其它 SNES 核心）不保证可读。升级核心前应保留 `.srm`。
- 不使用 RetroArch 之外模拟器的存档格式；不做自动转换。
- 真机写入、掉电一致性和与文件管理器的交互均未测。

## 已知问题与边界

- **MD 不支持 Sega CD / 32X。** 构建时关闭了 CD-ROM 支持，扫描器也不把
  `.32x` 列为 MD ROM。`.bin` 同时是 MD 卡带和光盘镜像的常见扩展名；
  放在 `md/` 下的 `.bin` 一律按卡带处理，Sega CD 光盘不会工作。
- **NES 的 FDS 需要 BIOS。** `.fds` 可以扫描和启动，但 Famicom Disk System
  通常需要 `disksys.rom`；当前没有为它提供自动查找路径，未实测。
- **SNES 特殊芯片与高分辨率模式。** Snes9x 覆盖常见增强芯片，但个别游戏的
  边角行为（逐行效果、特殊协处理器时序）未逐一验证。部分游戏使用 512×448
  隔行画面；在 1024×768 上整数缩放只能取 1 倍，画面会明显偏小，2 倍高度
  超出屏幕。是否改为 `video_scale_integer=false` 需真机看效果后再定。
- **许可证。** Snes9x 与 Genesis Plus GX 为非商业许可证，随镜像分发时
  不得用于商业用途；FCEUmm 为 GPL-2.0+。三者的许可证文件均随源码包
  校验（`LICENSE_FILES`）。
- **UI 名称。** 三个平台在界面显示为“任天堂 NES / 超级任天堂 / 世嘉 MD”；
  核心本身仍是上游项目，不使用容易误解为自研的命名。

## 复现与回归

```sh
# 主机扫描/注册表测试
dash tests/shell/test-platforms.sh

# 仅重建三个核心（在 Buildroot 构建目录）
make gameos-core-fceumm-rebuild gameos-core-snes9x-rebuild \
     gameos-core-genesis-plus-gx-rebuild

# 确认 ABI 与大小
ls -l output/target/usr/lib/libretro/{fceumm,snes9x,genesis_plus_gx}_libretro.so
aarch64-linux-gnu-nm -D output/target/usr/lib/libretro/*.so | grep ' T retro_run'
```

真机验证完成后，本文件的 `NOT RUN` 行应替换为实测数据、游戏名、测试时长
与日志路径；在完成之前，README 和发布说明不得声称这些平台已通过硬件测试。
