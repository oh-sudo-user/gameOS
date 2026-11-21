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

其它平台（GBA/GBC/PS1/PSP）单屏运行于上屏，下屏保持熄灭——
与真机插 GBA 卡时的行为一致。

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
- 前端：C++17，上屏轮播/游戏列表/设置，下屏氛围时钟；七语言界面
  （简中/繁中/日/英/俄/西/韩），Noto Sans SC/KR 字体，模拟器菜单语言同步。
- 渲染：每帧先录成绘制命令缓冲，可只重绘变化区域并跳过完全相同的帧；
  默认整屏重绘，测试时用 `GAMEOS_UI_DIRTY=1` 或 `/userdata/system/ui-dirty.on`
  开启局部重绘，像素结果由宿主比对测试保证一致。
- 模拟器：melonDS（JIT）、gpSP（arm64 dynarec，默认 GBA 核心）、mGBA（GB/GBC）、PCSX-ReARMed（dynarec）、PPSSPP（GLES 2x）。
- 数据分区：exFAT（GAMEOS 盘符），Windows/macOS 可直接拷游戏。

## 构建

Buildroot 2026.08 external tree；所有上游组件固定 commit/版本
（见 `scripts/fetch-vendor.sh` 与 defconfig）。发布镜像在本地干净构建并验证，
产物为整盘镜像 `gameOS-rgds-plus.img.gz`。