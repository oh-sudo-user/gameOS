# gameOS

A game system built from scratch for the Anbernic RG DS Plus and RG DS.
为 Anbernic RG DS Plus 与 RG DS 从零打造的游戏系统。

**v1.0.1 — bug-fix release / 问题修复版**

![gameOS](docs/assets/preview.png)

[English](#english) · [简体中文](#简体中文) · [Releases](https://github.com/oh-sudo-user/gameOS/releases) · [gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher)

## English

### What this is

gameOS turns the RG DS / RG DS Plus into a dedicated retro handheld: power on, land in your game library, pick a game, play. No desktop, no account.

Bootloader, kernel, root filesystem, front-end and device patches are all built from source in this repository. The front-end — icons, backgrounds, themes — is original C++17 on SDL2. There is no display server, no package manager, and no background service beyond the few this device actually needs.

Both models share one kernel and root filesystem and identify themselves at boot; the Plus drives dual 1024×768 panels, the DS dual 640×480 with the interface scaled. These handhelds have no internal storage and gameOS never writes to any — everything lives on the microSD card, so pulling the card leaves the device exactly as it was.

### What's new in 1.0.1

- **In-game menus stay on the top screen.** Every RetroArch (RGUI) page — main menu, settings, subscreens — is now drawn on the top panel only instead of being centred across the seam between the two screens; the bottom panel keeps showing the game.
- **Wi-Fi asks for the password.** Picking a secured network opens the on-screen keyboard again, and a failed attempt no longer leaves a keyless "saved" entry behind (which made later taps skip the prompt). The prompt, network name and masked input are mirrored on the top screen.
- **Box-art download from Settings.** The libretro cover scraper that used to be wizard-only now has its own Settings tile, with progress, stop and a status line; it keeps running if you leave the page.

### What's new in 1.0.0

- **A faster NDS core.** melonDS's ARM64 JIT now links blocks, caches CPU flags and invalidates 16 bytes at a time, tuned with profile-guided optimisation: 74.5 → 121.3 fps on the host bench (1.63x) — see Performance.
- **NDS starts at once.** ROM images are memory-mapped on demand, so the 8–10 s cartridge read is gone, and the launch animation hands over to the emulator without a black gap.
- **A polished shell.** Per-console launch animations, non-blocking dual-screen output, instant language switching, and Wi-Fi / Bluetooth / sleep handling that does not stall.

### What it plays

| System | Emulator | Notes |
|---|---|---|
| Nintendo DS | melonDS (ARM64 JIT) | Both screens, touch on the bottom one |
| Game Boy Advance | gpSP (ARM64 dynarec) | |
| Game Boy / Game Boy Color | mGBA | |
| PlayStation 1 | PCSX-ReARMed (ARM64 dynarec) | .cue / .m3u / .chd supported properly |
| PSP | PPSSPP (Mali-G52 via Panfrost) | 1x render resolution by default |

NDS, GBA, GBC and PS1 run comfortably on this chip. PSP is the heaviest thing it can do: most games are fine, but demanding titles will stutter. PPSSPP ships at 1x internal resolution; raise it in PPSSPP's own graphics settings if you want the sharper look.

NDS works out of the box: the melonDS core carries FreeBIOS, an open-source BIOS replacement, so no files are needed. Own console dumps (`bios7.bin`, `bios9.bin`, `firmware.bin`) in `bios/` take priority when present; a card in the TF2 slot (Plus) can supply them automatically.

### Performance

Numbers are labelled **measured** when we actually ran them, **calculated** when they are arithmetic, and **expected** when they follow from what the change does on this class of chip. "Host bench" means a headless static front-end under qemu-aarch64 on a PC, not the handheld.

| Area | Change | Effect |
|---|---|---|
| NDS · CPU core | melonDS 0.9.3 ARM64 JIT gains block linking, cached host CPU flags and 16-byte memory invalidation, plus a profile-guided (PGO) build | **measured** (host bench, Pokémon White 2, 600 frames): 74.5 → 121.3 fps, 1.63x; video and audio hashes identical to the unoptimised core |
| NDS · startup | ROM images are memory-mapped on demand instead of read up front | **measured** (host bench): 509 → 256 ms load; **expected** on card: the 8–10 s read becomes sub-second |
| NDS · rendering | threaded software renderer beside the JIT; dead OpenGL options removed | **expected:** 1.2–1.6x on render-bound scenes |
| NDS · output | non-blocking dual-screen page flip | removes the per-frame vblank wait; both panels stay in step |
| GBA · core | gpSP with ARM64 dynarec replaces mGBA (mGBA stays for GB/GBC) | **measured** (host bench, same ROM): ~1,500 fps vs ~360 fps — about 4x |
| PSP · resolution | 1x internal resolution by default (2x before) | **calculated:** 4x fewer pixels per frame (522,240 → 130,560) |
| PSP · readback | GPU→CPU frame copy skipped | **calculated:** ~522 KB/frame, ~31 MB/s at 60 fps, no longer copied |
| PSP · headroom | the same 1x setting | **measured** (host): DOOM gameplay at ~80 fps on a software rasteriser, so the core clears 60 fps with room to spare |
| Front-end | instant language switch; cheaper animation path | **measured** (host): switch ~1 ms, was ~14–16 ms; **expected:** 0.2–0.5 ms of animation per frame |
| Wi-Fi / Bluetooth | busy-waits and long timeouts removed | **measured:** a scan no longer idles ~13 s; actions no longer stall 5 s when the radio is off |

**Reading the table.** NDS and GBA gains are game-dependent: a scene that isn't render-bound, or a game that never idles, will see less. Every change above ships in this release. An on-device benchmark table is next on the list — nothing here is extrapolated into a device fps we did not measure.

### Two screens, used properly

In DS mode, melonDS renders both screens into one tall framebuffer and the kernel scans it out to both panels in a single commit: top screen on top, bottom screen below, touch mapped to where your finger lands. A clean integer scale — 4x on the Plus, 2x on the DS — so pixels stay square: no shimmer, no blur.

Every other system runs on the top screen with the bottom one dark, exactly like a real DS with a GBA cartridge. While you browse the library, the bottom screen becomes a quiet clock.

### Getting started

You need a microSD card, 8GB or bigger (A1/A2 recommended). Card quality matters more than you'd think — the boot ROM is picky about initialisation, and no-name or fake-capacity cards are the most common cause of a black screen.

> Flashing erases the card. Back up first.

**The easy way — [gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher).** A Windows tool that picks the right image for your model, downloads the latest release, verifies its SHA-256, decompresses and writes it — with system-disk protection. It can also flash a local `.img` / `.img.gz` file.

**The manual way.**

1. Download the image for your device from [Releases](https://github.com/oh-sudo-user/gameOS/releases): `gameOS-rgds-plus.img.gz` or `gameOS-rgds.img.gz`.
2. Write it as a whole-disk image. [balenaEtcher](https://etcher.balena.io/) reads the `.gz` directly; on Linux:

   ```bash
   gunzip -c gameOS-rgds-plus.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
   ```

   Replace `sdX` with the card's device (check `lsblk` first — this erases the whole card). Each image ships a `.sha256` sidecar.
3. Insert the card in the **TF1 slot** (next to the power button) and power on.
4. First boot takes 10–30 s longer: it creates a data partition on the rest of the card, then runs the setup wizard.
5. Pull the card and fill **GAMEOS-DATA** (exFAT, read natively by Windows and macOS):

```
GAMEOS-DATA/
├── roms/
│   ├── nds/     .nds  .dsi  .ids  .srl
│   ├── gba/     .gba  .agb  .mb
│   ├── gbc/     .gb  .gbc  .sgb
│   ├── ps1/     .cue  .m3u  .chd  .pbp  .iso  .bin
│   └── psp/     .iso  .cso  .pbp  .elf
├── bios/        bios7.bin  bios9.bin  firmware.bin   (optional, own dumps)
├── covers/      <system>/<game name>.png or .jpg      (optional box art)
├── saves/       emulator saves — browse/delete in Settings
└── system/      settings and emulator configuration
```

Subfolders are fine (up to four levels). For multi-disc PS1 games, keep the discs together and point an `.m3u` playlist at them; the library shows one entry. Box art is optional: name the image after the game file and put it in `covers/<system>/`.

### First boot and everyday use

The first power-on runs a short wizard: language, Wi-Fi (with a link / route / DNS / internet test), timezone, device name, optional RetroAchievements login and optional box-art download. **B** skips a step, **START** skips the wizard; everything can be changed later in Settings.

- **A launch sequence for every console.** A game card drops into a console on the top screen while the bottom reports percentage, sector address, transfer rate and a live graph; it ends on black exactly as the emulator takes over.
- **Wi-Fi.** WPA2/WPA3 via wpa_supplicant, managed from Settings with the gamepad keyboard; reconnects after sleep.
- **Updates over the air.** The latest release is checked at boot, written to the idle A/B slot after confirmation, and rolled back automatically if the new slot fails four times. Games and saves are never touched. OTA manifests are Ed25519-signed and verified before parsing — see [docs/OTA-SIGNING.md](docs/OTA-SIGNING.md).
- **A library that stays out of the way.** Favorites, play history, optional box art (local or downloaded), subfolders, and multi-disc PS1 games shown as one entry.
- **Activity.** Play time per game, a day-by-day timeline, trends and a "what to play next" pick.
- **Look and language.** Four complete themes (Neon, Pixel, Fresh, Ink) and six languages; both switch instantly, and emulator menus follow the language.
- **The rest.** Second card slot (Plus), save browser, Bluetooth audio, brightness and volume, battery indicator, lid sleep, clean low-battery shutdown, and a small chime for every action.

### Controls (front-end)

The home screen is a carousel: five systems, then **Activity**, **Quick** and **Settings**. Browse with left/right, open with A.

| Button | Action |
|---|---|
| D-pad | Move around; left/right browses the carousel |
| A | Confirm / launch |
| B | Back |
| START | Open Settings |
| X | Add/remove favorite |
| Y | Rescan the game list |
| R | Toggle favorites-only view |
| HOME | Back to the home screen |

In **Quick**, left/right switches between Favorites and History. In **Activity**, it switches overview, timeline, trends and picks. In Settings, up/down picks a row and left/right adjusts it; A opens the Wi-Fi, Bluetooth, update, save, power and about screens. Emulators keep their own controls and menus, and those menus follow the system language too.

### Under the hood

```
BootROM → idbloader (RK3568 DDR firmware + SPL)
        → U-Boot v2026.01 (SD-card-first, power-key boot, A/B slot script)
        → Linux 7.2.11 (mainline + pinned RK3566 patch set + per-model device tree)
        → Buildroot rootfs (busybox init)
        → gameos-splash (staged boot screen, both panels)
        → gameos-inputd + gameos-sleepd
        → gameos-ui (SDL2 straight to DRM/KMS)
        → RetroArch / PPSSPP
```

- **No display server.** Front-end and emulators draw straight to DRM/KMS.
- **One gamepad.** `gameos-inputd` merges buttons, sticks, volume keys and the lid switch into a single virtual pad, with the DS model's stick axes remapped.
- **Both panels ready at boot.** The dual-DSI driver is built into the kernel; the top panel is selected by connector name on both models.
- **A/B boot with rollback.** Two rootfs slots on the card; updates write the idle one.
- **Full clock by default.** Heat is the limit, not policy.
- **Per-console layers.** `game nds` / `game gba` / `game psp` handle launch, tuning, saves/BIOS and logging; the NDS layer adds the DSi-mode boot fix, the threaded renderer, and a block-linking, profile-guided ARM64 JIT.

Deeper details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · flashing guide: [docs/FLASHING.md](docs/FLASHING.md) · problems: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

### Building from source

You need an x86_64 Linux host, a few hours, and tens of GB of free disk for the first build.

```bash
git clone https://github.com/oh-sudo-user/gameOS.git
./gameOS/scripts/fetch-vendor.sh     # pinned kernel patches, device trees, DDR/BL31 firmware, fonts
git clone --depth 1 -b 2026.08 https://github.com/buildroot/buildroot.git
cd buildroot
make BR2_EXTERNAL=../gameOS/buildroot gameos_rgds_plus_defconfig
make -j$(nproc)
# output/images/: gameOS-rgds-plus.img.gz, gameOS-rgds.img.gz,
# plus the OTA payload (rootfs, boot, manifest.json)
```

All upstream components are pinned to exact versions or commits, so the same commit builds the same software. Release images are built locally from a clean tree, verified, and uploaded by hand.

### License

- gameOS' own code — front-end, daemons, build glue, device patches — is **GPL-3.0-or-later**.
- Integrated components keep their own licenses: Linux (GPL-2.0), U-Boot (GPL-2.0+), melonDS (GPL-3.0; its bundled FreeBIOS carries a BSD-style notice, reproduced in docs/TROUBLESHOOTING.md), RetroArch (GPL-3.0), PPSSPP (GPL-2.0+), mGBA (MPL-2.0), gpSP (GPL-2.0+), Noto and Orbitron fonts (OFL).
- The Rockchip DDR/BL31 firmware blobs are proprietary binaries, redistributed under the rkbin repository's license.

---

## 简体中文

### 这是什么

gameOS 把 RG DS / RG DS Plus 变成一台纯粹的游戏掌机：开机直接进游戏库，选游戏，按 A，开玩。没有桌面，没有强制账号。

引导程序、内核、根文件系统、前端和设备补丁全部从本仓库源码构建。前端——图标、背景、主题——是原创的 C++17 + SDL2 程序。没有显示服务器，没有包管理器，除了这台机器真正需要的几个守护进程之外没有别的后台服务。

两种机型共用同一个内核和根文件系统，开机时自动识别：Plus 驱动两块 1024×768 屏，DS 驱动两块 640×480 屏并缩放界面。这两台掌机没有内置存储，gameOS 也不往里写任何东西——一切都在 microSD 卡上，拔掉卡机器就恢复原样。

### 1.0.1 修复

- **模拟器菜单只在上屏显示。** RetroArch（RGUI）的主菜单、设置、各子页面不再跨两块屏居中、被接缝切开，统一画在上屏；下屏继续显示游戏画面。
- **Wi-Fi 会正常要求输入密码。** 选中加密网络会弹出屏幕键盘；连接失败不再留下"已保存但没密码"的空记录（那会让之后点同一网络直接跳过密码）。密码提示、网络名和掩码输入会同步显示在上屏。
- **设置里可以下载游戏封面。** 原来只在首次引导里有的 libretro 封面刮削，现在有独立的"游戏封面"设置项，带进度、停止和状态提示；离开页面后下载继续在后台进行。

### 1.0.0 新内容

- **更快的 NDS 核心。** melonDS 的 ARM64 JIT 加入块链接、CPU 标志缓存和 16 字节粒度失效，并用按实际运行的剖面优化（PGO）构建：电脑测试台 74.5 → 121.3 帧/秒（1.63 倍），详见"性能"。
- **NDS 即开即玩。** ROM 改为按需内存映射，8–10 秒的整卡读取没有了，启动动画和模拟器之间也不再黑屏。
- **打磨过的基线。** 每平台启动动画、双屏非阻塞输出、语言即时切换，以及不再卡顿的 Wi-Fi / 蓝牙 / 休眠处理。

### 能玩什么

| 平台 | 模拟器 | 说明 |
|---|---|---|
| 任天堂 DS | melonDS（ARM64 JIT） | 双屏直出，下屏触摸 |
| GBA | gpSP（ARM64 动态编译） | |
| GB / GBC | mGBA | |
| PS1 | PCSX-ReARMed（ARM64 动态编译） | 完整支持 .cue / .m3u / .chd |
| PSP | PPSSPP（Panfrost 驱动 Mali-G52） | 默认 1 倍渲染分辨率 |

NDS、GBA、GBC、PS1 在这颗芯片上都很从容。PSP 是它能干的最大的活：大部分游戏没问题，重负载作品会掉帧。PPSSPP 默认 1 倍内部分辨率；想要更锐利，可以在 PPSSPP 自己的图形设置里调高。

NDS 开箱即玩：melonDS 核心自带 FreeBIOS（开源的 BIOS 替代实现），不放任何文件也能玩。自己导出的 `bios7.bin`、`bios9.bin`、`firmware.bin` 放进 `bios/` 会优先使用；Plus 的 TF2 卡里有这些文件时会自动补齐。

### 性能

数字分三类：**实测**是真正跑出来的；**算出**是纯算术；**预期**是按这项改动在这类芯片上的作用推算的。"电脑测试台"指 PC 上用 qemu-aarch64 跑的无头静态前端，不是掌机。

| 环节 | 改动 | 效果 |
|---|---|---|
| NDS · CPU 核心 | melonDS 0.9.3 的 ARM64 JIT 加入块链接、宿主标志缓存、16 字节内存失效，并做 PGO（按实际运行剖面优化）构建 | **实测**（电脑测试台，宝可梦白 2，600 帧）：74.5 → 121.3 帧/秒，1.63 倍；画面与音频哈希和未优化核心完全一致 |
| NDS · 启动 | ROM 按需内存映射，不再一次性读入 | **实测**（电脑测试台）：加载 509 → 256 ms；**预期**在卡上：原来 8–10 秒的读取变成亚秒级 |
| NDS · 渲染 | JIT 旁边跑线程化软件渲染器；删掉无效的 OpenGL 选项 | **预期：**渲染吃紧的场景 1.2–1.6 倍 |
| NDS · 输出 | 双屏翻页改为非阻塞 | 不再每帧等垂直同步；两屏保持同步 |
| GBA · 核心 | gpSP（ARM64 动态编译）取代 mGBA（GB/GBC 仍用 mGBA） | **实测**（电脑测试台，同 ROM）：约 1500 帧/秒 vs 约 360 帧/秒——约 4 倍 |
| PSP · 分辨率 | 默认 1 倍内部分辨率（此前 2 倍） | **算出：**每帧像素少 4 倍（522,240 → 130,560） |
| PSP · 回读 | 跳过 GPU→CPU 的整帧回读 | **算出：**每帧约 522 KB、60 帧/秒下约 31 MB/s 不再搬运 |
| PSP · 模拟余量 | 就是上面这个 1 倍设置 | **实测**（电脑）：DOOM 游戏画面在软件光栅器上约 80 帧/秒，核心本身远超 60 帧 |
| 前端 | 语言即时切换；每帧动画开销更省 | **实测**（电脑）：切换约 1 ms，此前约 14–16 ms；**预期：**每帧 0.2–0.5 ms 动画开销 |
| Wi-Fi / 蓝牙 | 去掉忙等和过长的超时 | **实测：**扫描不再空等约 13 秒；蓝牙关闭时操作不再卡 5 秒 |

**怎么读这张表。** NDS 和 GBA 的收益天然看游戏：渲染不吃紧的场景、从不空转的游戏，变化会小。表里的改动全部包含在本版本中。掌机实测表是下一步计划，这里不会把没测过的数字冒充设备帧率。

### 双屏，用在刀刃上

DS 模式下，melonDS 把两个画面渲染进一块竖长帧缓冲，内核一次提交同时扫到两块屏：上屏在上、下屏在下，触摸坐标落在手指真正点的地方。整数缩放——Plus 4 倍、DS 2 倍，像素方方正正：不抖、不糊。

其他平台都在上屏运行，下屏熄灭——和真机插 GBA 卡时一样。浏览游戏库的时候，下屏是一台安静的时钟。

### 上手

准备一张 8GB 以上的 microSD 卡（推荐 A1/A2）。卡的质量比想象中重要——BootROM 对卡的初始化很挑剔，杂牌卡、扩容卡是黑屏最常见的原因。

> 刷机会清空整张卡，请先备份。

**省事的方式——[gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher)。** Windows 图形化工具：按机型选镜像、下载最新 release、SHA-256 校验、解压、写入，全程自动，并带系统盘保护；也支持刷本地 `.img` / `.img.gz`。

**手动方式。**

1. 从 [Releases](https://github.com/oh-sudo-user/gameOS/releases) 下载对应机型的镜像：`gameOS-rgds-plus.img.gz` 或 `gameOS-rgds.img.gz`。
2. 按整盘镜像写入。[balenaEtcher](https://etcher.balena.io/) 可直接刷 `.gz`；Linux 下：

   ```bash
   gunzip -c gameOS-rgds-plus.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
   ```

   把 `sdX` 换成卡的真实设备名（先用 `lsblk` 看清——这会擦掉整张卡）。每个镜像附带 `.sha256` 校验文件。
3. 卡插 **TF1 槽**（靠电源键的那个），开机。
4. 第一次开机会多花 10–30 秒：在卡的剩余空间建数据分区，然后进设置向导。
5. 拔卡插电脑，往 **GAMEOS-DATA** 盘里放游戏（exFAT，Windows 和 macOS 都原生读写）：

```
GAMEOS-DATA/
├── roms/
│   ├── nds/     .nds  .dsi  .ids  .srl
│   ├── gba/     .gba  .agb  .mb
│   ├── gbc/     .gb  .gbc  .sgb
│   ├── ps1/     .cue  .m3u  .chd  .pbp  .iso  .bin
│   └── psp/     .iso  .cso  .pbp  .elf
├── bios/        bios7.bin  bios9.bin  firmware.bin   (可选，自己导出)
├── covers/      <平台>/<游戏名>.png 或 .jpg           (可选封面)
├── saves/       模拟器存档——设置里可浏览/删除
└── system/      设置与模拟器配置
```

子文件夹最多四层。多盘 PS1 游戏把盘放一起，用 `.m3u` 播放列表指过去，游戏库里只显示一条。封面可选：图片名与游戏文件名一致，放进 `covers/<平台>/`。

### 首次开机与日常使用

第一次开机会走一个简短向导：语言、Wi-Fi（链路 / 路由 / DNS / 外网四段测试）、时区、机器名、可选的 RetroAchievements 登录和可选的在线封面下载。**B** 跳过当前步骤，**START** 跳过整个向导；所有设置之后都能在设置里改。

- **每台机器的启动仪式。** 打开游戏时上屏会插进一张游戏卡，下屏同步报告百分比、扇区地址、传输速率和活动图；动画在模拟器接手的那一刻正好落到全黑。
- **Wi-Fi。** wpa_supplicant 支持 WPA2/WPA3，全部在设置里用手柄键盘操作；休眠唤醒后自动重连。
- **OTA 更新。** 联网开机检查最新 release，确认后写入空闲的 A/B 槽；新槽四次启动失败会自动回滚。更新不碰游戏和存档。OTA 清单使用 Ed25519 签名，设备解析前先验签，详见 [docs/OTA-SIGNING.md](docs/OTA-SIGNING.md)。
- **省心的游戏库。** 收藏、游玩历史、可选封面（本地或在线下载）、子文件夹，多盘 PS1 游戏合成一条。
- **文件管理。** 内置双卡文件管理器：浏览、复制、移动、重命名、新建文件夹、删除，TF1/TF2 互拷；ZIP 先解到临时目录再原子落盘，拒绝路径穿越、符号链接、加密包和 ZIP64，删除、覆盖与挂载 TF2 可写都要二次确认。
- **Activity 统计。** 总时长与单游戏时长、逐日时间线、趋势，以及"下一款玩什么"推荐。
- **外观与语言。** 四套完整主题（霓虹、像素、清新、水墨）和六种语言，都是即时切换；模拟器菜单也跟随语言。
- **其余。** 双卡槽（Plus）、存档浏览器、蓝牙音频、亮度音量、电量显示、合盖休眠、低电量正常关机，以及每个操作一声小音效。

### 按键（前端界面）

主界面是轮播：五个平台，然后 **Activity**、**快捷**、**设置**。左右浏览，A 打开。

| 按键 | 作用 |
|---|---|
| 方向键 | 移动；主界面左右浏览轮播 |
| A | 确认 / 启动 |
| B | 返回 |
| START | 打开设置 |
| X | 收藏 / 取消收藏 |
| Y | 重新扫描游戏列表 |
| R | 只看收藏 |
| HOME | 回主界面 |

**快捷**里左右切换收藏/历史；**Activity** 里左右切换概览、时间线、趋势、推荐。设置里上下选行、左右调节，A 进入 Wi-Fi、蓝牙、更新、存档、关机、关于等页面。游戏里各模拟器保留自己的按键和菜单，菜单语言同样跟随系统。

### 底层结构

```
BootROM → idbloader（RK3568 DDR 固件 + SPL）
        → U-Boot v2026.01（SD 卡优先、电源键开机、A/B 槽位脚本）
        → Linux 7.2.11（主线内核 + 固定版本 RK3566 补丁集 + 按机型的设备树）
        → Buildroot 根文件系统（busybox init）
        → gameos-splash（分阶段开机画面，双屏）
        → gameos-inputd + gameos-sleepd
        → gameos-ui（SDL2 直写 DRM/KMS）
        → RetroArch / PPSSPP
```

- **没有显示服务器。** 前端和模拟器直写 DRM/KMS。
- **一个手柄。** `gameos-inputd` 把按键、摇杆、音量键、合盖开关合并成一个虚拟手柄，DS 机型的摇杆轴序也做了重映射。
- **双屏开机即就绪。** 双 DSI 面板驱动直接编进内核；两种机型都按连接器名字选中上屏。
- **A/B 启动与回滚。** 卡上两个根文件系统槽；更新写空闲槽。
- **默认满频。** 限制是散热，不是策略。
- **按机种分层。** `game nds` / `game gba` / `game psp` 负责启动流程、调优、存档/BIOS 归置和日志；NDS 层还包含 DSi 增强卡带的 DS 模式启动修复、线程化渲染器，以及带块链接和 PGO 的 ARM64 JIT。

更多细节：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · 刷机教程：[docs/FLASHING.md](docs/FLASHING.md) · 遇到问题：[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)。

### 从源码构建

需要一台 x86_64 Linux 主机、几个小时，首次构建还要几十 GB 空闲磁盘。

```bash
git clone https://github.com/oh-sudo-user/gameOS.git
./gameOS/scripts/fetch-vendor.sh     # 固定版本的内核补丁、设备树、DDR/BL31 固件、字体
git clone --depth 1 -b 2026.08 https://github.com/buildroot/buildroot.git
cd buildroot
make BR2_EXTERNAL=../gameOS/buildroot gameos_rgds_plus_defconfig
make -j$(nproc)
# output/images/：gameOS-rgds-plus.img.gz、gameOS-rgds.img.gz，
# 以及 OTA 载荷（rootfs、boot、manifest.json）
```

所有上游组件都锁定到确切版本或 commit，同一个 commit 编出来的系统软件版本完全一致。发布镜像在本地干净的构建树中编译、验证，然后手动上传。

### 许可证

- gameOS 自研代码——前端、守护进程、构建脚本、设备补丁——采用 **GPL-3.0-or-later**。
- 集成的上游组件各自遵循其许可证：Linux（GPL-2.0）、U-Boot（GPL-2.0+）、melonDS（GPL-3.0；其内置 FreeBIOS 为 BSD 风格许可，声明全文见 docs/TROUBLESHOOTING.md）、RetroArch（GPL-3.0）、PPSSPP（GPL-2.0+）、mGBA（MPL-2.0）、gpSP（GPL-2.0+）、Noto 与 Orbitron 字体（OFL）。
- Rockchip DDR/BL31 固件是专有二进制，按 rkbin 仓库的许可分发。
