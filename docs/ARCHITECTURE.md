# gameOS 架构说明

## 设计目标

1. **能启动**（第一原则）：引导链每一环都选用在 RG DS Plus 上被验证过的组合。
2. **性能**：无桌面环境、无显示服务器，SDL2/RetroArch 直写 DRM/KMS；
   模拟器全部启用 ARM64 动态编译或 GPU 加速。
3. **精致**：自研多语言前端（6 种语言）+ 全套原创图标/背景。

## 硬件要点（RG DS Plus）

- SoC：Rockchip RK3568（4× Cortex-A55 + Mali-G52），1GB RAM，**无 eMMC**。
- 双 4.3 寸 1024×768 DSI 屏：dsi0→vp0（上屏）、dsi1→vp1（下屏）。
- 下屏 GT911 触摸（1024×768）。
- 输入设备在 Linux 下是**分裂的**：按键（gpio-keys-control）、
  摇杆（adc-joystick）、音量键、霍尔盖开关各自独立 evdev 节点。
- 音频：rk817 codec（耳机）+ aw88166 功放（扬声器）。

## 引导链（本机验证过的组合）

```
BootROM
  → sector 64:    idbloader.img = mkimage -T rksd (rk3568_ddr_1560MHz_v1.26.bin + SPL)
  → sector 16384: u-boot.itb    = U-Boot proper + ATF BL31 (rk3568_bl31_v1.46.elf)
  → U-Boot v2026.01 (quartz64-a-rk3566 基础配置 + 两处 RG DS Plus 修正 + SPL MMC)
  → extlinux.conf: KERNEL /Image  FDT /rk3568-anbernic-rg-ds-plus.dtb
  → Linux 7.2.11 (主线内核 + 固定版本 RK3566 补丁集 + 本设备 DTS)
```

SPL 阶段黑屏的病根是引导配置与 DDR/板级代码不匹配。
gameOS 选用为 RG DS Plus 验证过的引导路径，并补齐其缺口：

- 基础配置不含 Anbernic 板级探测代码（该代码在主线 ATF 下会崩溃重启）；
- 补丁 1：RK8XX PMIC 改为电源键触发启动（掌机必需）；
- 补丁 2：SPL 引导顺序 SD 卡优先（本机无 eMMC）；
- 补丁 3（gameOS 自有）：`CONFIG_SPL_MMC=y`。quartz64 原配置的 SPL 只会从
  SPI flash 读下一级，而本机无 SPI；缺了这一项 SPL 会报
  "Unsupported Boot Device" 并永远停在黑屏。raw 模式偏移 0x4000 扇区
  与 u-boot.itb 的写入位置一致（Kconfig 对 ARCH_ROCKCHIP 的默认值）；
- DDR 初始化使用 RK3568 官方 v1.26 blob（不是 RK3566 的）；
- 内核设备树由 extlinux **显式指定**，不依赖 U-Boot 的 ADC 机型探测。

## 内核与双屏驱动

- 内核源码：主线 v7.2.11（kernel.org，与 vendored RK3566 补丁集匹配）。
- 补丁：通用 default + RK3566 设备补丁集（固定 commit），
  由 `kernel-patches/series` 锁定应用顺序；
  在 post-extract 钩子中以 `patch -p1`（fuzz 2）语义应用
  （Buildroot 自带补丁步为零 fuzz，会拒绝其中 5 个需要 fuzz 的上游补丁）。
- 设备树：`rk3568-anbernic-rg-ds.dtsi` / `.dts` / `-plus.dts`（vendored），
  在全部补丁应用之后覆盖进内核树。
- 面板驱动：`panel-generic-dsi` 以 **obj-y
  编入内核**（非模块），DRM 绑定前双屏时序即就绪，解析 DTS 的 panel_description。

## 输入合并（gameos-inputd）

把分裂的 evdev 设备合并为一个 uinput 虚拟手柄 **"gameOS Gamepad"**：
18 个按键 + 归一化摇杆轴；udev 规则隐藏原始设备避免 SDL/RetroArch 误识别。
附带：音量键直接驱动 ALSA。合盖休眠由 gameos-sleepd 负责（见用户空间一节）。

## 双屏 NDS（本项目核心补丁）

RetroArch 的 KMS/DRM context 补丁（`GAMEOS_DUALSCREEN=1` 时启用）：

- 创建 **一块 1024×1536 的 GBM framebuffer**；
- 一次 atomic modesetting 提交把它同时扫出到两个 CRTC：
  上屏 plane 取 FB 的 [0,768) 行，下屏 plane 取 [768,1536) 行；
- melonDS 核心竖排布局 256×384 → **4 倍整数缩放**，一屏一个 NDS 画面；
- 触摸补丁把下屏触摸坐标映射到 y∈[768,1536)，即 NDS 下屏。

其它平台单屏运行于上屏，下屏保持熄灭——与真机插 GBA 卡时的行为一致。
（DS 模式下 NDS 双屏直出；NES/SNES/MD 也走上屏。）

## 平台与核心来源

前端 `src/library.cpp` 的注册表是唯一平台清单；启动时按 `id` 找到对应
核心与配置。子系统名统一为 `game <id>`（如 `game nes`），只是日志/脚本层
的命名，**不代表该核心为自研**。

| id | 平台 | 运行方式 | 核心/程序 | 来源与许可证 |
|---|---|---|---|---|
| nds | Nintendo DS | RetroArch | `melonds_libretro.so` | melonDS 上游 + gameOS 的 JIT/内存映射补丁 + PGO，GPL-3.0+ |
| gba | GBA | RetroArch | `gpsp_libretro.so` | libretro/gpSP 上游 + gameOS PGO 构建，GPL-2.0+ |
| gbc | GB / GBC | RetroArch | `mgba_libretro.so` | mGBA 0.10.5 上游 + gameOS PGO 构建，MPL-2.0 |
| ps1 | PlayStation | RetroArch | `pcsx_rearmed_libretro.so` | libretro/pcsx_rearmed 上游（ari64 dynarec + neon GPU；PGO 无收益，保持默认），GPL-2.0+ |
| psp | PSP | 独立进程 | `ppsspp-run` → PPSSPP v1.20.4 | PPSSPP 上游，GPL-2.0+ |
| nes | NES / Famicom | RetroArch | `fceumm_libretro.so` | libretro-fceumm `77468e5` + gameOS PGO 构建，GPL-2.0+ |
| snes | SNES | RetroArch | `snes9x_libretro.so` | libretro/snes9x `204ea1c` + gameOS PGO 构建，Snes9x 非商业许可证 |
| md | Mega Drive / Genesis | RetroArch | `genesis_plus_gx_libretro.so` | Genesis-Plus-GX `4fd32ef` + gameOS PGO 构建，非商业许可证；`HAVE_CDROM=0` |

所有核心都沿用上游引擎，**不是把模拟器重写一遍**；gameOS 的做法与
`game nds` 相同：上游源码 + gameOS 补丁/构建调优 + 按真实运行剖面优化
（PGO），并用同一款 ROM 的前后哈希确认画面与声音逐字节一致。每个核心的
做法、实测收益与副作用见 [CORES.md](CORES.md)。每核心的版本、依赖、产物
大小、已支持格式与未验证项见 [PLATFORMS.md](PLATFORMS.md)。`md` 不包含
Sega CD / 32X 支持，因此扫描器不把 `.32x` 当作 MD ROM。

三个新核心由 Buildroot 外部树中的 `gameos-core-fceumm`、
`gameos-core-snes9x`、`gameos-core-genesis-plus-gx` 构建，各包内 `pgo/`
存放剖面计数，配方以 `-fprofile-use` 引用；产物安装到
`/usr/lib/libretro/`；RetroArch 通过 `nes.cfg` / `snes.cfg` / `md.cfg`
选择整数缩放、关闭平滑。RetroArch 核心清单与前端注册表必须同时增加
平台，`tests/host/test-platforms.cpp` 会校验 id、扩展名唯一性以及
同目录 `.md`/`.bin` 去重。

## 用户空间

```
busybox init
  ├─ rcS → S00splash   (双屏开机动画守护进程；内核 console 只留在串口，
  │                     面板不显示任何启动文字)
  │        S20gameos   (首 boot 扩容 exFAT 数据分区、性能 governor、
  │                     背光/音量默认值；面板驱动已编入内核无需加载)
  │        S27sleepd   (合盖暂停游戏并休眠、开盖恢复；充电时只熄屏；
  │                     RTC 定时醒来防电池睡干)
  └─ respawn: gameos-session
       ├─ gameos-inputd &
       └─ gameos-ui     (自研 SDL2 前端；退出码 90=关机 91=重启)
            ├─ 启动前写 /run/gameos/splash.stop 并等 splash 退出，
            │  接管 DRM master（splash 先淡入 UI 背景再释放，无黑缝）
            └─ 启动游戏时 SDL_Quit 释放 DRM master → fork/exec
               retroarch / ppsspp-run → 退出后重新接管双屏
```

- 图形栈：mesa3d Panfrost (Mali-G52) + EGL/GLES2，SDL2 KMSDRM，无 X11/Wayland。
- 前端：C++17，上屏轮播/游戏列表/设置，下屏氛围时钟；六语言界面
  （简中/繁中/日/英/俄/西），Noto Sans SC/KR 字体，模拟器菜单语言同步。
- 渲染：每帧先录成绘制命令缓冲，可只重绘变化区域并跳过完全相同的帧；
  默认整屏重绘，测试时用 `GAMEOS_UI_DIRTY=1` 或 `/userdata/system/ui-dirty.on`
  开启局部重绘，像素结果由宿主比对测试保证一致。
- 组件：`tokens.hpp` 固定间距/行高/圆角/字号；`widget` 提供列表行、
  按钮、开关、滑块、弹层、Toast 的绘制与纯布局/命中函数
  （行命中、滑块取值、弹层按钮排布、Toast 队列）；游戏库上下两屏的游戏行
  以及 WiFi/蓝牙/存档/统计/最爱/时区/账号等下半屏列表的行内容已改用组件
  绘制（各页原有底色、选中条与触摸命中保持原逻辑），全局按钮也已统一走
  `widget`，其余页面逐步迁移。
- 模块拆分：`netparse` 把 `gameos-bt` / `gameos-wifi` 的文本输出解析
  （MAC 校验、设备行、扫描去重排序、saved/keyed 标记）从 `main.cpp`
  独立出来，纯标准库实现，由宿主单测覆盖；`main.cpp` 只保留交互与状态。
- 每游戏设置：`overrides` 读写 `/userdata/system/game_overrides.json`
  （schema 1，json-c 校验；整份损坏改名为 `.bak`，单条非法只跳过该条），
  游戏键为「机种:绝对路径|文件大小」。设置页把整数缩放/平滑缩放/GBA 帧跳
  映射为 RetroArch appendconfig，性能档位走与全局相同的 perf 档；列表控制栏
  的齿轮（或 START）打开，改动即时原子写入，启动前还会按 mtime 热加载。
- 文件管理：`fileops` 负责双卡路径守卫（`clean_rel` / `resolve_under` /
  `within_roots`，拒绝绝对路径、`..` 和符号链接逃逸）、目录列举、
  复制/移动/删除/改名/建目录，以及后台 `TaskQueue` 单工作线程执行复制与
  解压（进度、速度、剩余时间、可安全取消）。根目录只有 `/userdata` 与
  `/mnt/tf2`；TF2 默认只读，写操作先确认，`mount -o remount,rw` 后执行，
  队列清空立即 `remount,ro`，回只读失败按 fail-closed 处理（下次写重新询问）。
  ZIP 先解到目标父目录下的 `.gameos-unzip-*` 暂存区，逐项校验 CRC 与大小，
  拒绝 ZIP64、加密、符号链接、重复名、绝对名和越界路径，完成后原子改名
  落盘。界面为单窗格加 TF1/TF2 切换，复制与移动默认目标是另一张卡。
- 网络传输：`netsftp` 封装 libssh2，在工作线程里完成 SFTP 目录列举与下载，
  UI 只读取进度快照。主机指纹按 OpenSSH 格式 SHA256 写入
  `/userdata/system/sftp_known_hosts` 做 TOFU 固定，不匹配立即中止；
  服务器信息存 `sftp.ini`（0600，原子写），密码只保留在内存。下载前检查
  远端大小与本地剩余空间，写 `.part` 并从已有长度续传，校验精确大小后
  rename 落盘；取消/失败保留 `.part` 供重试。远端路径按 `/` 规范化并拒绝
  `..` 越界，本地文件名剥掉目录成分，重名自动加序号。当前只实现 SFTP
  密码登录；SMB/FTP、上传与递归下载不在本版本内。
- 性能与电源：`gameos-perf` 只写 governor 名称，从不写频率。策略名为
  `ui` / `powersave` / `balanced` / `performance` / `superdrastic`，
  `light` / `heavy` 是 `balanced` / `performance` 的兼容别名；CPU 逐个
  policy 读取 `scaling_available_governors` 后选择并回退，GPU 默认 `KEEP`。
  `gameos-sleepd` 每 30 秒调用 `gameos-perf guard`：任一热区达到 85 °C
  或电量 ≤15% 时把 performance 档压到 balanced，达到 95 °C 或 ≤8% 时
  全部压到 powersave；温度回到 80 °C 且电量恢复 20% 才释放。阈值在
  `/etc/gameos/perf.conf`，`GUARD_ENABLE=0` 可整体关闭，内核 thermal
  框架仍负责硬件降频。档位切换写入 `system/logs/perf.log`；请求和守护状态
  在 `/run/gameos/perf-request`、`perf-guard`。`superdrastic` 暂与
  performance 使用同一 governor，因为没有实测的频率下限可供设置。
- 触摸：下屏每帧重建命中区；移动超过 12 DP 视为拖动，不再触发点击；页面
  转场期间丢弃触摸，避免点按落在新页面刚建立的命中区上；抬起时重新命中
  同一控件才执行动作。
- 动效：时长按 80/120/160/180/220/320 ms 分档，缓动只有
  enter/exit/move/spring/linear 五条（60 点采样表）；每帧采样主线程 CPU
  时间，30 帧窗口 P95 超过 4 ms 时按 L0→L3 逐级降级（页面转场在 L1
  去缩放、L2 只淡入淡出、L3 硬切），连续 120 帧低于预算一半才回退一级，
  回退后 300 帧内不再变化；级别变化写入 `system/logs/ui.log`。
- 启动路径：点游戏后先对模拟器、核心、GL 库、ROM 前段和存档发
  `posix_fadvise(WILLNEED)` 预读，让磁盘读取与 0.9 s 插卡动画并行；
  只预热 ROM 的前 8–16 MB，其余仍按需读盘。RetroArch 关闭 8 MB 中文字体
  的三份加载（`video_font_enable=false`，rgui 自带位图字体）和载入动画；
  NDS ROM 用 mmap 懒加载，512 MB 卡带不再整份读进内存；NDS 的
  `--verbose` 改为仅在 `system/ra-verbose` 存在时开启。
- 主题调色板：`assets/themes/<key>/palette.conf`（schema v1：七个
  `#rrggbb` 颜色 + 面板透明度/染色三个字节）。开机 splash 与 UI 共用
  同一文件；缺字段、重复键或非法值会让整个文件被忽略并回退到内置色板。
  `scripts/lint-themes.sh` 在测试中校验四个主题，并确认文件与内置值一致。
- 模拟器：melonDS（JIT）、gpSP（arm64 dynarec，默认 GBA 核心）、mGBA（GB/GBC）、
  PCSX-ReARMed（dynarec）、PPSSPP（GLES 2x），以及 NES/SNES/MD 的 FCEUmm /
  Snes9x / Genesis Plus GX。除 PPSSPP 外都沿用上游引擎，gameOS 在此基础上
  做补丁、构建调优和 PGO；PCSX-ReARMed 的热代码由运行时 dynarec 生成，
  PGO 无收益，保持上游默认。详见 [CORES.md](CORES.md)。
- 数据分区：exFAT（GAMEOS 盘符），Windows/macOS 可直接拷游戏。

## 构建

Buildroot 2026.08 external tree；所有上游组件固定 commit/版本
（见 `scripts/fetch-vendor.sh` 与 defconfig）。发布镜像在本地干净构建并验证，
产物为整盘镜像 `gameOS-rgds-plus.img.gz`。

`scripts/ci.sh` 汇总主机侧门禁：全部 shell/host 测试、主题调色板校验、
脚本语法检查与包版本锁定审计（拒绝浮动 git 引用）。设置 `GAMEOS_IMAGES`
后会调用 `scripts/verify-image.sh` 校验成品：必需产物与 sha256、gzip 完整性、
启动包内容、目标二进制/字体/OTA 公钥，以及从 `rootfs.ext4` 直接读取的
四套主题调色板（`debugfs`；无镜像时回退到 target 树）。两者均为普通
`sh` 脚本，可在本地或任意 CI 中运行。