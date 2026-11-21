# gameOS

A game system built from scratch for the Anbernic RG DS Plus and RG DS.
为 Anbernic RG DS Plus 与 RG DS 从零打造的游戏系统。

**v1.1.0**

![gameOS](docs/assets/preview.png)

[English](#english) · [简体中文](#简体中文) · [Releases](https://github.com/oh-sudo-user/gameOS/releases) · [gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher)

## English

### What this is

gameOS turns the RG DS / RG DS Plus into a dedicated retro handheld: power on, land in your game library, pick a game, play. No desktop, no account.

Bootloader, kernel, root filesystem and front-end are all built from source in this repository. There is no display server — the front-end and emulators draw straight to the dual screens. On DS titles both panels are used as one tall surface (1024×1536 on the Plus); everything else plays on the top screen with the bottom one dark, exactly like a real DS with a GBA cartridge. The handhelds have no internal storage, so everything lives on the microSD card.

### What it plays

| System | Emulator | Notes |
|---|---|---|
| Nintendo DS | melonDS (ARM64 JIT) | Both screens, touch on the bottom one |
| Game Boy Advance | gpSP (ARM64 dynarec) | |
| Game Boy / Game Boy Color | mGBA | |
| PlayStation 1 | PCSX-ReARMed (ARM64 dynarec) | .cue / .m3u / .chd |
| PSP | PPSSPP (Mali-G52 via Panfrost) | 1x render resolution by default |
| NES / Famicom | FCEUmm | gameOS profile-guided build |
| SNES | Snes9x | gameOS profile-guided build |
| Sega Mega Drive / Genesis | Genesis Plus GX | gameOS profile-guided build; no Sega CD / 32X |

The engines are mature upstream libretro cores; gameOS ships them with its own patches, build tuning and profile-guided optimisation, and verifies that the output is byte-identical to the stock build. Per-core evidence: [docs/CORES.md](docs/CORES.md).

### Performance

All numbers are **host bench**: a headless front-end under qemu-aarch64 on a PC. They are not handheld frame rates; on-device temperature and power have not been measured yet.

| Area | Result |
|---|---|
| NDS · JIT + PGO | 74.5 → 121.3 fps (1.63x), video/audio hashes identical |
| NDS · load | 509 → 256 ms (ROMs memory-mapped on demand) |
| GBA · gpSP | ~1,450 fps vs ~360 fps for mGBA — about 4x |
| SNES · PGO | 292 → 342 fps (+17%) |
| Mega Drive · PGO | 522 → 633 fps (+21%) |
| GBA / GB · mGBA PGO | GBA +6–8%; GB ~+1% |
| NES · PGO | even within run noise |
| PS1 · PCSX-ReARMed | unchanged by design — its hot code is dynarec-generated at runtime, so profiles cannot reach it |
| PSP | 1x internal resolution: 4x fewer pixels per frame |

### Getting started

You need a microSD card, 8GB or bigger (A1/A2 recommended). Flashing erases the card — back up first.

1. Download `gameOS-rgds-plus.img.gz` or `gameOS-rgds.img.gz` from [Releases](https://github.com/oh-sudo-user/gameOS/releases).
2. Write it as a whole-disk image: the [gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher) does it on Windows, or use balenaEtcher / `gunzip -c … | sudo dd of=/dev/sdX bs=4M conv=fsync`.
3. Insert the card in the **TF1 slot** (next to the power button) and power on. First boot takes 10–30 s longer while the data partition is created.
4. Pull the card and fill the exFAT **GAMEOS-DATA** partition:

```
roms/{nds,gba,gbc,ps1,psp,nes,snes,md}/   game files (subfolders OK)
bios/                                     optional own dumps (bios7/9.bin, firmware.bin)
covers/<system>/<game>.png|jpg            optional box art
saves/  system/  network/                 saves, settings, SFTP downloads
```

The first power-on runs a short wizard: language, Wi-Fi, timezone, device name, optional RetroAchievements and box art. Everything can be changed later in Settings.

### Everyday use

Wi-Fi (WPA2/WPA3), SFTP download from your PC, signed OTA updates with automatic rollback, a dual-card file manager with secure ZIP extraction, favorites, play history and box art, four themes, six languages (emulator menus follow), per-game performance profiles, lid sleep and low-battery shutdown. All logs are written to `system/logs/` on the card, so a device can be diagnosed from the card alone.

**Front-end controls:** D-pad move · **A** confirm · **B** back · **START** Settings · **X** favorite · **Y** rescan games · **R** favorites only · **HOME** home screen. Emulators keep their own controls and menus.

Deeper details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · flashing: [docs/FLASHING.md](docs/FLASHING.md) · troubleshooting: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

### Building from source

x86_64 Linux host, a few hours and tens of GB of disk for the first build.

```bash
git clone https://github.com/oh-sudo-user/gameOS.git
./gameOS/scripts/fetch-vendor.sh
git clone --depth 1 -b 2026.08 https://github.com/buildroot/buildroot.git
cd buildroot
make BR2_EXTERNAL=../gameOS/buildroot gameos_rgds_plus_defconfig
make -j$(nproc)
# output/images/: gameOS-rgds-plus.img.gz, gameOS-rgds.img.gz + OTA payload
```

All upstream components are pinned to exact versions or commits.

### License

gameOS' own code is **GPL-3.0-or-later**. Integrated components keep their licences (Linux, U-Boot, RetroArch, PPSSPP, melonDS: GPL; mGBA: MPL-2.0; gpSP, FCEUmm: GPL-2.0+; Snes9x and Genesis Plus GX: upstream non-commercial). The Rockchip DDR/BL31 blobs are proprietary but redistributable under the rkbin licence.

## 简体中文

### 这是什么

gameOS 把 RG DS / RG DS Plus 变成一台纯粹的游戏掌机：开机直接进游戏库，选游戏，按 A，开玩。没有桌面，没有强制账号。

引导程序、内核、根文件系统和前端全部从本仓库源码构建。没有显示服务器，前端和模拟器直写双屏。DS 游戏把上下两屏拼成一块竖长画面（Plus 为 1024×1536）；其他平台在上屏运行、下屏熄灭，和真机插 GBA 卡时一样。掌机没有内置存储，一切都在 microSD 卡上。

### 能玩什么

| 平台 | 模拟器 | 说明 |
|---|---|---|
| 任天堂 DS | melonDS（ARM64 JIT） | 双屏直出，下屏触摸 |
| GBA | gpSP（ARM64 动态编译） | |
| GB / GBC | mGBA | |
| PS1 | PCSX-ReARMed（ARM64 动态编译） | .cue / .m3u / .chd |
| PSP | PPSSPP（Panfrost 驱动 Mali-G52） | 默认 1 倍渲染分辨率 |
| NES / 红白机 | FCEUmm | gameOS 剖面优化构建 |
| SNES / 超任 | Snes9x | gameOS 剖面优化构建 |
| 世嘉 MD / Genesis | Genesis Plus GX | gameOS 剖面优化构建；不支持 Sega CD / 32X |

引擎都是成熟的上游 libretro 核心；gameOS 负责补丁、构建调优和按真实运行剖面的 PGO，并用哈希验证输出与原版逐字节一致。逐核心证据见 [docs/CORES.md](docs/CORES.md)。

### 性能

所有数字都来自**电脑测试台**（PC 上 qemu-aarch64 跑的无头前端），不是掌机帧率；掌机的温度与功耗尚未实测。

| 环节 | 结果 |
|---|---|
| NDS · JIT + PGO | 74.5 → 121.3 帧/秒（1.63 倍），画面/音频哈希一致 |
| NDS · 加载 | 509 → 256 ms（ROM 按需内存映射） |
| GBA · gpSP | 约 1450 帧/秒 vs mGBA 约 360 帧/秒——约 4 倍 |
| SNES · PGO | 292 → 342 帧/秒（+17%） |
| 世嘉 MD · PGO | 522 → 633 帧/秒（+21%） |
| GBA / GB · mGBA PGO | GBA +6–8%；GB 约 +1% |
| NES · PGO | 在运行噪声内持平 |
| PS1 · PCSX-ReARMed | 有意不改——热点是 dynarec 运行时生成的代码，剖面够不着 |
| PSP | 1 倍内部分辨率：每帧像素少 4 倍 |

### 上手

准备一张 8GB 以上的 microSD 卡（推荐 A1/A2）。刷机会清空整张卡，请先备份。

1. 从 [Releases](https://github.com/oh-sudo-user/gameOS/releases) 下载 `gameOS-rgds-plus.img.gz` 或 `gameOS-rgds.img.gz`。
2. 整盘写入：[gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher)（Windows 图形化），或 balenaEtcher / `gunzip -c … | sudo dd of=/dev/sdX bs=4M conv=fsync`。
3. 插进 **TF1 卡槽**（电源键旁）开机。首次启动会多花 10–30 秒创建数据分区。
4. 拔卡，把游戏拷进 exFAT 的 **GAMEOS-DATA** 分区：

```
roms/{nds,gba,gbc,ps1,psp,nes,snes,md}/   游戏文件（支持子文件夹）
bios/                                     可选，自备 BIOS（bios7/9.bin、firmware.bin）
covers/<system>/<game>.png|jpg            可选，封面
saves/  system/  network/                 存档、设置、SFTP 下载
```

首次开机会走一个简短向导：语言、Wi-Fi、时区、机器名、可选的 RetroAchievements 和封面下载。所有设置之后都能在设置里改。

### 日常使用

Wi-Fi（WPA2/WPA3）、从电脑 SFTP 下载游戏、带自动回滚的签名 OTA 更新、双卡文件管理与安全 ZIP 解压、收藏/游玩历史/封面、四套主题、六种语言（模拟器菜单同步）、每游戏性能档位、合盖休眠与低电量关机。所有运行日志都写在卡的 `system/logs/`，拔卡即可取日志排障。

**前端按键：** 方向键移动 · **A** 确认 · **B** 返回 · **START** 设置 · **X** 收藏 · **Y** 重扫游戏 · **R** 只看收藏 · **HOME** 回主界面。游戏里各模拟器保留自己的按键和菜单。

更多细节：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · 刷机教程：[docs/FLASHING.md](docs/FLASHING.md) · 遇到问题：[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)。

### 从源码构建

需要 x86_64 Linux 主机、几个小时，首次构建还要几十 GB 空闲磁盘。

```bash
git clone https://github.com/oh-sudo-user/gameOS.git
./gameOS/scripts/fetch-vendor.sh
git clone --depth 1 -b 2026.08 https://github.com/buildroot/buildroot.git
cd buildroot
make BR2_EXTERNAL=../gameOS/buildroot gameos_rgds_plus_defconfig
make -j$(nproc)
# output/images/：gameOS-rgds-plus.img.gz、gameOS-rgds.img.gz 以及 OTA 载荷
```

所有上游组件都锁定到确切版本或 commit。

### 许可证

gameOS 自研代码采用 **GPL-3.0-or-later**。集成组件各自遵循其许可证（Linux、U-Boot、RetroArch、PPSSPP、melonDS：GPL；mGBA：MPL-2.0；gpSP、FCEUmm：GPL-2.0+；Snes9x 与 Genesis Plus GX：上游非商业许可证）。Rockchip DDR/BL31 为专有二进制，按 rkbin 仓库的许可证分发。
