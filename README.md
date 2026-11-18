# gameOS

A game system built from scratch for one handheld: the Anbernic RG DS Plus.
为 Anbernic RG DS Plus 从零打造的游戏系统。

![gameOS](docs/assets/preview.png)

[English](#english) · [简体中文](#简体中文)

## English

### What this is

gameOS turns the RG DS Plus into a dedicated retro handheld. Power on and you are in your game library — no desktop, no setup wizard, no account. Pick a game, press A, play.

The whole system is built from source in this repository: bootloader, kernel, root filesystem, front-end, and the device-specific patches that make this handheld work. The front-end — including its icons and backgrounds — is original work, written in C++17 on SDL2. And the system is deliberately small: no display server, no package manager, no background services beyond the few this device actually needs.

One thing worth knowing up front: the RG DS Plus has no internal storage, and gameOS never writes to any. Everything lives on the microSD card. Pull the card and the device is back to exactly how it was — so feel free to experiment.

### What it plays

| System | Emulator | Notes |
|---|---|---|
| Nintendo DS | melonDS (ARM64 JIT) | Both screens, touch on the bottom one |
| Game Boy Advance | mGBA | |
| Game Boy / Game Boy Color | mGBA | |
| PlayStation 1 | PCSX-ReARMed (ARM64 dynarec) | Handles .cue / .m3u / .chd properly |
| PSP | PPSSPP (Mali-G52 via Panfrost) | 2x render resolution by default |

A word on expectations: NDS, GBA, GBC and PS1 run comfortably on this chip. PSP is the heaviest thing it can do — most games are fine, but demanding titles will stutter. Dropping PPSSPP's render resolution to 1x in its own settings usually helps.

NDS games need three BIOS files dumped from your own console: `bios7.bin`, `bios9.bin` and `firmware.bin`. They are copyrighted, so they are not included. If any are missing, the front-end tells you exactly which ones when you try to launch a DS game — nothing cryptic.

### Two screens, used properly

This is the part I cared about most. In DS mode, melonDS renders both screens into one tall framebuffer, and the kernel scans it out to both panels in a single commit: top screen on top, bottom screen on the bottom, touch mapped to where your finger actually lands. It is a clean 4x integer scale, so pixels stay square — no shimmer, no blur.

Every other system runs on the top screen with the bottom one dark, which is exactly what a real DS does when you slot in a GBA cartridge. While you browse the library, the bottom screen becomes a quiet clock instead.

### Everyday features

- **Seven languages.** 简体中文, 繁體中文, 日本語, English, Русский, Español, 한국어. Switch in Settings and the change is instant — the emulator menus follow along too.
- **A library that stays out of the way.** Favorites (and a favorites-only view), optional box art, subfolders for organizing, and multi-disc PS1 games shown as one entry instead of five.
- **Save management.** Browse and delete save files from Settings — no card reader needed.
- **Comfort.** Four color themes, brightness and volume adjustment, battery indicator, and Bluetooth audio for headphones and speakers.
- **Close the lid, keep your place.** The game pauses and the device sleeps; open the lid and you are back where you were. While charging it just turns the screens off. If the battery runs low during sleep, it wakes on a timer and shuts down cleanly rather than dying mid-save.

### Getting started

You need a microSD card, 16GB or bigger. A decent one matters more than you would think — the boot ROM is picky about card initialization, and no-name or fake-capacity cards are the most common cause of a black screen. Samsung, Kioxia and SanDisk cards have the best track record here.

1. Download the latest `gameOS-rgds-plus.img.gz` from [Releases](https://github.com/oh-sudo-user/gameOS/releases).
2. Write it to the card as a whole-disk image. Any imaging tool works; on Linux:

   ```bash
   gunzip -c gameOS-rgds-plus.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
   ```

   Replace `sdX` with your card's actual device (check `lsblk` first — this erases the whole card). Write the image; don't copy files onto a formatted card.
3. Put the card in the **TF1 slot** (the one next to the power button) and power on.
4. First boot takes 10–30 seconds longer than usual: it creates a data partition filling the rest of the card. Later boots are quick.
5. Pull the card, plug it into a computer. A drive named **GAMEOS-DATA** shows up (exFAT — Windows and macOS both read it natively). Drop your games in:

```
GAMEOS-DATA/
├── roms/
│   ├── nds/     .nds  .dsi  .ids  .srl
│   ├── gba/     .gba  .agb  .mb
│   ├── gbc/     .gb  .gbc  .sgb
│   ├── ps1/     .cue  .m3u  .chd  .pbp  .iso  .bin
│   └── psp/     .iso  .cso  .pbp  .elf
├── bios/        bios7.bin  bios9.bin  firmware.bin   (NDS, your own dumps)
├── covers/      <system>/<game name>.png or .jpg      (optional box art)
├── saves/       emulator saves — browse/delete in Settings
└── system/      settings and emulator configuration
```

Subfolders are fine (up to four levels deep). For multi-disc PS1 games, keep the discs together and point an `.m3u` playlist at them; the library lists the game once. Box art is optional: name the image after the game (its filename without extension) and put it in `covers/<system>/`.

### Controls (front-end)

| Button | Action |
|---|---|
| D-pad | Move around; left/right browses systems on the home screen |
| A | Confirm / launch |
| B | Back |
| START | Open Settings |
| X | Add/remove favorite |
| Y | Rescan the game list |
| R | Toggle favorites-only view |
| HOME | Back to the home screen |

In Settings, up/down picks a row and left/right adjusts it (brightness, volume, theme, language); A opens the Bluetooth, save, power and about screens. Inside a game, each emulator keeps its own controls and menus — those menus follow the system language too.

### Under the hood

```
BootROM → idbloader (RK3568 DDR firmware + SPL)
        → U-Boot v2026.01 (SD-card-first, power-key boot)
        → Linux 7.2.11 (mainline + pinned RK3566 patch set + RG DS Plus device tree)
        → Buildroot rootfs (busybox init)
        → gameos-inputd + gameos-sleepd
        → gameos-ui (SDL2 straight to DRM/KMS)
        → RetroArch / PPSSPP
```

A few decisions worth noting:

- **No display server.** The front-end and emulators write straight to DRM/KMS. Less to go wrong, and nothing between you and the pixels.
- **One gamepad.** Linux sees this device's buttons, analog stick, volume keys and lid switch as separate input nodes. `gameos-inputd` merges them into a single virtual gamepad, so emulators don't have to guess.
- **Both panels ready at boot.** The dual-DSI panel driver is built into the kernel, not loaded as a module — no probe race, no flicker.
- **Performance governors by default.** The chip runs at full clock; heat is the limit, not policy.

Deeper details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · flashing guide: [docs/FLASHING.md](docs/FLASHING.md) · problems: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

### Building from source

You need an x86_64 Linux host, a few hours, and tens of GB of free disk for the first build. Or don't build at all: every push to `main` triggers GitHub Actions, which builds the image and publishes it to Releases.

```bash
git clone https://github.com/oh-sudo-user/gameOS.git
./gameOS/scripts/fetch-vendor.sh     # pinned kernel patches, device trees, DDR/BL31 firmware, fonts
git clone --depth 1 -b 2026.08 https://github.com/buildroot/buildroot.git
cd buildroot
make BR2_EXTERNAL=../gameOS/buildroot gameos_rgds_plus_defconfig
make -j$(nproc)
# result: output/images/gameOS-rgds-plus.img.gz
```

All upstream components are pinned to exact versions or commits, so the same commit always builds the same software versions.

### License

- gameOS' own code — front-end, input/sleep daemons, build glue, device patches — is **GPL-3.0-or-later**.
- Integrated components keep their own licenses: Linux (GPL-2.0), U-Boot (GPL-2.0+), melonDS (GPL-3.0), RetroArch (GPL-3.0), PPSSPP (GPL-2.0+), mGBA (MPL-2.0), Noto and Orbitron fonts (OFL).
- The Rockchip DDR/BL31 firmware blobs are proprietary binaries, redistributed under the rkbin repository's license.

---

## 简体中文

### 这是什么

gameOS 把 RG DS Plus 变成一台纯粹的游戏掌机。开机直接进游戏库——没有桌面、没有设置向导、不用注册账号。选游戏，按 A，开玩。

整个系统都在这个仓库里从源码构建：引导程序、内核、根文件系统、前端界面，以及让这台掌机正常工作的那些设备补丁。前端界面——包括全套图标和背景——都是原创作品，用 C++17 和 SDL2 写成。系统也刻意做得很小：没有显示服务器，没有包管理器，除了这台机器真正需要的几个守护进程之外，没有别的后台服务。

有件事值得先说清楚：RG DS Plus 没有内置存储，gameOS 也不会往里面写任何东西。一切都在 microSD 卡上。拔掉卡，机器就恢复原样——所以放心折腾。

### 能玩什么

| 平台 | 模拟器 | 说明 |
|---|---|---|
| 任天堂 DS | melonDS（ARM64 JIT） | 双屏直出，下屏触摸 |
| GBA | mGBA | |
| GB / GBC | mGBA | |
| PS1 | PCSX-ReARMed（ARM64 动态编译） | 完整支持 .cue / .m3u / .chd |
| PSP | PPSSPP（Panfrost 驱动 Mali-G52） | 默认 2 倍渲染分辨率 |

说句实话：NDS、GBA、GBC、PS1 在这颗芯片上都很从容。PSP 是它能干的最大的活——大部分游戏没问题，但重负载作品会掉帧。进 PPSSPP 自己的设置把渲染分辨率降到 1 倍，通常就顺了。

NDS 游戏需要三个从你自己的主机上导出的 BIOS 文件：`bios7.bin`、`bios9.bin`、`firmware.bin`。它们有版权，所以不内置。缺哪个，启动 DS 游戏时界面会明明白白告诉你缺哪个，不绕弯子。

### 双屏，用在刀刃上

这是我最上心的部分。DS 模式下，melonDS 把两个画面渲染进一块竖长的帧缓冲，内核一次提交就把它同时扫到两块屏上：上屏在上、下屏在下，触摸坐标落在你手指真正点的地方。干净的 4 倍整数缩放，像素方方正正——不抖、不糊。

其他平台都在上屏运行，下屏熄灭——和真机插 GBA 卡时的行为一模一样。浏览游戏库的时候，下屏则是一台安静的时钟。

### 日常功能

- **七种语言。** 简体中文、繁體中文、日本語、English、Русский、Español、한국어。在设置里切换，立刻生效——连模拟器自己的菜单也跟着变。
- **省心的游戏库。** 收藏（和只看收藏）、可选封面图、子文件夹整理，多盘 PS1 游戏只显示一条，不会列出五条。
- **存档管理。** 在设置里浏览、删除存档，不用拔卡。
- **用得舒服。** 四套配色主题、亮度音量调节、电量显示，蓝牙耳机/音箱音频。
- **合盖不丢进度。** 合上盖子，游戏暂停、机器休眠；开盖回到刚才的地方。充电时合盖只熄屏。休眠中电量不足的话，它会定时醒来正常关机，而不是睡到一半没电。

### 上手

准备一张 16GB 以上的 microSD 卡。卡比你想的重要——BootROM 对卡的初始化时序很挑剔，杂牌卡、扩容卡是黑屏的最常见原因。Samsung、Kioxia、SanDisk 的正品卡在这里的成功率最高。

1. 从 [Releases](https://github.com/oh-sudo-user/gameOS/releases) 下载最新的 `gameOS-rgds-plus.img.gz`。
2. 用任意整盘镜像写入工具把镜像写进卡。Linux 下可以：

   ```bash
   gunzip -c gameOS-rgds-plus.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
   ```

   把 `sdX` 换成你的卡的真实设备名（先用 `lsblk` 看清楚——这会擦掉整张卡）。是"写镜像"，不是往格式化好的卡里"拷文件"。
3. 卡插 **TF1 槽**（靠电源键的那个），开机。
4. 第一次开机会多花 10–30 秒：它要在卡的剩余空间上建数据分区。以后就快了。
5. 拔卡插电脑，会出现一个叫 **GAMEOS-DATA** 的盘（exFAT，Windows 和 macOS 都原生读写）。把游戏放进去：

```
GAMEOS-DATA/
├── roms/
│   ├── nds/     .nds  .dsi  .ids  .srl
│   ├── gba/     .gba  .agb  .mb
│   ├── gbc/     .gb  .gbc  .sgb
│   ├── ps1/     .cue  .m3u  .chd  .pbp  .iso  .bin
│   └── psp/     .iso  .cso  .pbp  .elf
├── bios/        bios7.bin  bios9.bin  firmware.bin   (NDS 用，自己导出)
├── covers/      <平台>/<游戏名>.png 或 .jpg           (可选封面)
├── saves/       模拟器存档——设置里可浏览/删除
└── system/      设置与模拟器配置
```

子文件夹随便建（最多四层）。多盘 PS1 游戏把盘放一起，用一个 `.m3u` 播放列表指过去，游戏库里就只有一条。封面可选：图片名和游戏名（去掉扩展名的文件名）一致，放进 `covers/<平台>/` 就行。

### 按键（前端界面）

| 按键 | 作用 |
|---|---|
| 方向键 | 移动；主界面左右切换平台 |
| A | 确认 / 启动 |
| B | 返回 |
| START | 打开设置 |
| X | 收藏 / 取消收藏 |
| Y | 重新扫描游戏列表 |
| R | 只看收藏 |
| HOME | 回主界面 |

设置里上下选行、左右调节（亮度、音量、主题、语言）；A 进入蓝牙、存档、关机、关于等页面。游戏里各模拟器保留自己的按键和菜单——菜单语言同样跟随系统语言。

### 底层结构

```
BootROM → idbloader（RK3568 DDR 固件 + SPL）
        → U-Boot v2026.01（SD 卡优先、电源键开机）
        → Linux 7.2.11（主线内核 + 固定版本 RK3566 补丁集 + RG DS Plus 设备树）
        → Buildroot 根文件系统（busybox init）
        → gameos-inputd + gameos-sleepd
        → gameos-ui（SDL2 直写 DRM/KMS）
        → RetroArch / PPSSPP
```

几个值得一提的决定：

- **没有显示服务器。** 前端和模拟器直写 DRM/KMS。少一层就少一层出错的可能，你和像素之间也不隔东西。
- **一个手柄。** Linux 把这台机器的按键、摇杆、音量键、合盖开关看成各自独立的输入设备。`gameos-inputd` 把它们合并成一个虚拟手柄，模拟器不用猜。
- **双屏开机即就绪。** 双 DSI 面板驱动直接编进内核（不是模块），没有探测竞态，不会闪。
- **默认性能模式。** CPU/GPU 满频跑，限制是散热，不是策略。

更多细节：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · 刷机教程：[docs/FLASHING.md](docs/FLASHING.md) · 遇到问题：[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)。

### 从源码构建

需要一台 x86_64 Linux 主机、几个小时，首次构建还要几十 GB 空闲磁盘。或者干脆不编：每次推送到 `main`，GitHub Actions 都会自动编好镜像并发布到 Releases。

```bash
git clone https://github.com/oh-sudo-user/gameOS.git
./gameOS/scripts/fetch-vendor.sh     # 固定版本的内核补丁、设备树、DDR/BL31 固件、字体
git clone --depth 1 -b 2026.08 https://github.com/buildroot/buildroot.git
cd buildroot
make BR2_EXTERNAL=../gameOS/buildroot gameos_rgds_plus_defconfig
make -j$(nproc)
# 产物：output/images/gameOS-rgds-plus.img.gz
```

所有上游组件都锁定到确切版本或 commit，同一个 commit 编出来的系统，软件版本完全一致。

### 许可证

- gameOS 自研代码——前端、输入/休眠守护进程、构建脚本、设备补丁——采用 **GPL-3.0-or-later**。
- 集成的上游组件各自遵循其许可证：Linux（GPL-2.0）、U-Boot（GPL-2.0+）、melonDS（GPL-3.0）、RetroArch（GPL-3.0）、PPSSPP（GPL-2.0+）、mGBA（MPL-2.0）、Noto 与 Orbitron 字体（OFL）。
- Rockchip DDR/BL31 固件是专有二进制，按 rkbin 仓库的许可分发。