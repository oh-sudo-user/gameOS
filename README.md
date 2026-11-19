# gameOS

A game system built from scratch for two handhelds: the Anbernic RG DS Plus and the Anbernic RG DS.
为 Anbernic RG DS Plus 与 Anbernic RG DS 从零打造的游戏系统。

![gameOS](docs/assets/preview.png)

[English](#english) · [简体中文](#简体中文) · [Releases](https://github.com/oh-sudo-user/gameOS/releases) · [gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher)

## English

### What this is

gameOS turns the RG DS / RG DS Plus into a dedicated retro handheld. Power on and you are in your game library — no desktop, no mandatory account. Pick a game, press A, play.

The whole system is built from source in this repository: bootloader, kernel, root filesystem, front-end, and the device-specific patches that make these handhelds work. The front-end — including every icon, background and theme — is original work, written in C++17 on SDL2. And the system is deliberately small: no display server, no package manager, no background services beyond the few this device actually needs.

One image per model, one system underneath: both devices share the same kernel and root filesystem and detect which model they are at boot. The Plus drives dual 1024×768 panels; the DS drives dual 640×480 panels, with the interface scaled to match.

One thing worth knowing up front: these handhelds have no internal storage, and gameOS never writes to any. Everything lives on the microSD card. Pull the card and the device is back to exactly how it was — so feel free to experiment.

### What it plays

| System | Emulator | Notes |
|---|---|---|
| Nintendo DS | melonDS (ARM64 JIT) | Both screens, touch on the bottom one |
| Game Boy Advance | mGBA | |
| Game Boy / Game Boy Color | mGBA | |
| PlayStation 1 | PCSX-ReARMed (ARM64 dynarec) | Handles .cue / .m3u / .chd properly |
| PSP | PPSSPP (Mali-G52 via Panfrost) | 2x render resolution by default |

A word on expectations: NDS, GBA, GBC and PS1 run comfortably on this chip. PSP is the heaviest thing it can do — most games are fine, but demanding titles will stutter. Dropping PPSSPP's render resolution to 1x in its own settings usually helps.

NDS works out of the box: the melonDS core ships FreeBIOS, an open-source BIOS replacement, so no files are needed to play. For maximum compatibility you can still drop your own console dumps (`bios7.bin`, `bios9.bin`, `firmware.bin`) into the `bios/` folder — they take priority when present. A game card in the TF2 slot (Plus) can supply them automatically.

### Two screens, used properly

This is the part I cared about most. In DS mode, melonDS renders both screens into one tall framebuffer, and the kernel scans it out to both panels in a single commit: top screen on top, bottom screen on the bottom, touch mapped to where your finger actually lands. A clean integer scale — 4x on the Plus, 2x on the DS — so pixels stay square: no shimmer, no blur.

Every other system runs on the top screen with the bottom one dark, which is exactly what a real DS does when you slot in a GBA cartridge. While you browse the library, the bottom screen becomes a quiet clock instead.

### First boot

The first power-on runs a short setup wizard, and the boot animation shows what stage it is at instead of a wall of console text:

1. **Language** — seven to choose from.
2. **Wi-Fi** — pick a network, type the passphrase with the gamepad keyboard; a four-stage connectivity test (link / route / DNS / internet) tells you it really works.
3. **Timezone** — a curated list, applied immediately.
4. **Device name** — what the device calls itself on the network.
5. **RetroAchievements** — optional login for achievements (see below).
6. **Covers** — optional online box-art download for your library.

**B** skips the current step, **START** skips the whole wizard. Everything can be changed later in Settings, and the wizard never runs again once finished.

### Everyday features

- **Wi-Fi.** WPA2/WPA3 via wpa_supplicant, managed entirely from Settings with a gamepad-friendly keyboard. Reconnects automatically after sleep.
- **System updates over the air.** When online, gameOS checks the latest GitHub release at boot. It only ever offers something *newer* than what is running, asks for your confirmation, then writes the update into the idle boot slot. If the new slot fails to come up four times, the device rolls back to the old one on its own. Updates never touch your games or saves.
- **RetroAchievements.** RetroArch is built with achievement support (including hardcore mode). Log in from the wizard or Settings; your password is never stored — only the login token is kept.
- **Activity.** Total and per-game play time, a day-by-day timeline, trends, and a "what to play next" pick — all computed from your actual play history.
- **Quick access.** A favorites list and a play-history list, both launchable directly, one button away from the home screen.
- **A library that stays out of the way.** Favorites (and a favorites-only view), optional box art, subfolders for organizing, and multi-disc PS1 games shown as one entry instead of five.
- **Covers, two ways.** Drop your own images into `covers/`, or let the wizard / Settings download them online (official libretro thumbnails with mirror fallbacks, verified to be real PNGs before saving).
- **Four complete themes.** Neon, Pixel, Fresh and Ink — each one a full art set: home and ambient backgrounds, console icons, logo. Switching themes swaps the whole look, not just a color tint.
- **Six languages.** 简体中文, 繁體中文, 日本語, English, Русский, Español. Switch in Settings and the change is instant — the emulator menus follow along too.
- **Two card slots (Plus).** Games on a second card in TF2 are scanned and merged into the same library automatically; missing NDS BIOS files are copied over from it when present.
- **Save management.** Browse and delete save files from Settings — no card reader needed.
- **Comfort.** Brightness and volume adjustment, battery indicator, and Bluetooth audio for headphones and speakers (off by default, paired on demand).
- **Small sounds, big feel.** Moving, confirming, going back, favoriting — each action has its own little music-box chime. Same family, different notes, never in the way.
- **Close the lid, keep your place.** The game pauses and the device sleeps; open the lid and you are back where you were. While charging it just turns the screens off. If the battery runs low during sleep, it wakes on a timer and shuts down cleanly rather than dying mid-save.

### Getting started

You need a microSD card, 8GB or bigger (A1/A2 recommended). A decent one matters more than you would think — the boot ROM is picky about card initialization, and no-name or fake-capacity cards are the most common cause of a black screen. Samsung, Kioxia and SanDisk cards have the best track record here.

> Flashing erases everything on the card. Back up first.

**The easy way — [gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher).** A graphical tool for Windows (macOS build coming) that picks the right image for your device, downloads the latest release, verifies its SHA-256 twice, decompresses and writes it — with system-disk protection so you cannot accidentally overwrite your computer's drive. It can also flash a local `.img` / `.img.gz` file.

**The manual way.**

1. Download the image for your device from [Releases](https://github.com/oh-sudo-user/gameOS/releases):
   - RG DS Plus → `gameOS-rgds-plus.img.gz`
   - RG DS → `gameOS-rgds.img.gz`
2. Write it to the card as a whole-disk image. [balenaEtcher](https://etcher.balena.io/) reads the `.gz` directly; on Linux:

   ```bash
   gunzip -c gameOS-rgds-plus.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
   ```

   Replace `sdX` with your card's actual device (check `lsblk` first — this erases the whole card). Write the image; don't copy files onto a formatted card. Each image ships a `.sha256` sidecar if you want to verify the download.
3. Put the card in the **TF1 slot** (the one next to the power button) and power on.
4. First boot takes 10–30 seconds longer than usual: it creates a data partition filling the rest of the card, then runs the setup wizard. Later boots are quick.
5. Pull the card, plug it into a computer. A drive named **GAMEOS-DATA** shows up (exFAT — Windows and macOS both read it natively). Drop your games in:

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

Subfolders are fine (up to four levels deep). For multi-disc PS1 games, keep the discs together and point an `.m3u` playlist at them; the library lists the game once. Box art is optional: name the image after the game (its filename without extension) and put it in `covers/<system>/`.

### Controls (front-end)

The home screen is a carousel: the five systems, then **Activity**, **Quick** and **Settings**. Browse with left/right, open with A.

| Button | Action |
|---|---|
| D-pad | Move around; left/right browses the home carousel |
| A | Confirm / launch |
| B | Back |
| START | Open Settings |
| X | Add/remove favorite |
| Y | Rescan the game list |
| R | Toggle favorites-only view |
| HOME | Back to the home screen |

In **Quick**, left/right switches between the Favorites and History tabs. In **Activity**, left/right switches between overview, timeline, trends and picks. In Settings, up/down picks a row and left/right adjusts it (brightness, volume, theme, language); A opens the Wi-Fi, Bluetooth, update, save, power and about screens. Inside a game, each emulator keeps its own controls and menus — those menus follow the system language too.

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

A few decisions worth noting:

- **No display server.** The front-end and emulators write straight to DRM/KMS. Less to go wrong, and nothing between you and the pixels.
- **One gamepad.** Linux sees this device's buttons, analog sticks, volume keys and lid switch as separate input nodes. `gameos-inputd` merges them into a single virtual gamepad — with the DS model's stick axes remapped to match — so emulators don't have to guess.
- **Both panels ready at boot.** The dual-DSI panel driver is built into the kernel, not loaded as a module — no probe race, no flicker. The top panel is selected by connector name on both models, so single-screen games land on the lid screen where they belong.
- **A/B boot with rollback.** The card carries two rootfs slots; updates write the idle one and the boot script tries it, falling back to the previous slot after four unconfirmed boots.
- **Performance governors by default.** The chip runs at full clock; heat is the limit, not policy.

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
# results in output/images/:
#   gameOS-rgds-plus.img.gz + gameOS-rgds.img.gz   (card images, one per model)
#   gameOS-rootfs.ext4.gz + gameOS-boot.tar.gz + manifest.json   (OTA payload)
```

All upstream components are pinned to exact versions or commits, so the same commit always builds the same software versions. GitHub Actions builds both images on every push to `main` (downloadable as workflow artifacts); official Releases are published manually from verified builds, and a tag build never overwrites a release that already has assets.

### License

- gameOS' own code — front-end, input/sleep daemons, build glue, device patches — is **GPL-3.0-or-later**.
- Integrated components keep their own licenses: Linux (GPL-2.0), U-Boot (GPL-2.0+), melonDS (GPL-3.0; its bundled Drastic FreeBIOS carries a BSD-style notice, reproduced in docs/TROUBLESHOOTING.md), RetroArch (GPL-3.0), PPSSPP (GPL-2.0+), mGBA (MPL-2.0), Noto and Orbitron fonts (OFL).
- The Rockchip DDR/BL31 firmware blobs are proprietary binaries, redistributed under the rkbin repository's license.

---

## 简体中文

### 这是什么

gameOS 把 RG DS / RG DS Plus 变成一台纯粹的游戏掌机。开机直接进游戏库——没有桌面、没有强制注册的账号。选游戏，按 A，开玩。

整个系统都在这个仓库里从源码构建：引导程序、内核、根文件系统、前端界面，以及让这两台掌机正常工作的那些设备补丁。前端界面——包括每一个图标、背景和主题——都是原创作品，用 C++17 和 SDL2 写成。系统也刻意做得很小：没有显示服务器，没有包管理器，除了这台机器真正需要的几个守护进程之外，没有别的后台服务。

每个机型一个镜像，底层是同一套系统：两台机器共用同一个内核和根文件系统，开机时自动识别机型。Plus 驱动两块 1024×768 屏；DS 驱动两块 640×480 屏，界面按比例缩放适配。

有件事值得先说清楚：这两台掌机没有内置存储，gameOS 也不会往里面写任何东西。一切都在 microSD 卡上。拔掉卡，机器就恢复原样——所以放心折腾。

### 能玩什么

| 平台 | 模拟器 | 说明 |
|---|---|---|
| 任天堂 DS | melonDS（ARM64 JIT） | 双屏直出，下屏触摸 |
| GBA | mGBA | |
| GB / GBC | mGBA | |
| PS1 | PCSX-ReARMed（ARM64 动态编译） | 完整支持 .cue / .m3u / .chd |
| PSP | PPSSPP（Panfrost 驱动 Mali-G52） | 默认 2 倍渲染分辨率 |

说句实话：NDS、GBA、GBC、PS1 在这颗芯片上都很从容。PSP 是它能干的最大的活——大部分游戏没问题，但重负载作品会掉帧。进 PPSSPP 自己的设置把渲染分辨率降到 1 倍，通常就顺了。

NDS 开箱即玩：melonDS 核心自带 FreeBIOS（开源的 BIOS 替代实现），不放任何文件也能玩。想要最佳兼容性，仍可把自己主机导出的 `bios7.bin`、`bios9.bin`、`firmware.bin` 放进 `bios/` 文件夹——存在时优先使用。Plus 的 TF2 游戏卡里若有这些文件，会自动拷过来。

### 双屏，用在刀刃上

这是我最上心的部分。DS 模式下，melonDS 把两个画面渲染进一块竖长的帧缓冲，内核一次提交就把它同时扫到两块屏上：上屏在上、下屏在下，触摸坐标落在你手指真正点的地方。干净的整数缩放——Plus 4 倍、DS 2 倍，像素方方正正：不抖、不糊。

其他平台都在上屏运行，下屏熄灭——和真机插 GBA 卡时的行为一模一样。浏览游戏库的时候，下屏则是一台安静的时钟。

### 首次开机

第一次开机会走一个简短的设置向导，开机动画会显示当前进行到哪一步，而不是满屏滚动的启动文字：

1. **语言**——七种可选。
2. **Wi-Fi**——选网络、用手柄键盘输密码；四段连通性测试（链路 / 路由 / DNS / 外网）告诉你是不是真的通了。
3. **时区**——精选列表，立即生效。
4. **机器名**——设备在网络上的名字。
5. **RetroAchievements**——可选的成就账号登录（见下文）。
6. **封面**——可选的在线封面下载。

**B** 跳过当前步骤，**START** 跳过整个向导。所有设置之后都能在设置里改，向导完成后不会再出现。

### 日常功能

- **Wi-Fi。** wpa_supplicant 支持 WPA2/WPA3，全部在设置里用手柄键盘操作，休眠唤醒后自动重连。
- **OTA 系统更新。** 联网后开机自动检查 GitHub 最新 release，只提示比当前*更新*的版本，经你确认后写入空闲启动槽。新槽若四次都没能正常启动，机器自动回滚到旧槽。更新不碰游戏和存档。
- **RetroAchievements 成就。** RetroArch 内置成就支持（含 hardcore 模式）。向导或设置里登录；密码绝不落盘——只保留登录令牌。
- **Activity 统计。** 总时长与单游戏时长、逐日时间线、趋势，以及"下一款玩什么"推荐——全部来自你的真实游玩记录。
- **快捷入口。** 收藏列表和历史游玩列表，都能直接启动，主界面一键即达。
- **省心的游戏库。** 收藏（和只看收藏）、可选封面图、子文件夹整理，多盘 PS1 游戏只显示一条，不会列出五条。
- **封面，两种来路。** 自己放图进 `covers/`，或者让向导/设置在线下载（官方 libretro 封面源 + 镜像回退，确认是真 PNG 才保存）。
- **四套完整主题。** 霓虹夜景、像素复古、清新晨光、水墨山水——每套都是完整素材：主背景与氛围背景、平台图标、logo。换主题是换整套风格，不是调个色。
- **六种语言。** 简体中文、繁體中文、日本語、English、Русский、Español。在设置里切换，立刻生效——连模拟器自己的菜单也跟着变。
- **双卡槽（Plus）。** TF2 第二张卡里的游戏自动扫描、合并进同一个游戏库；卡里若有 NDS BIOS 且系统缺失，会自动补齐。
- **存档管理。** 在设置里浏览、删除存档，不用拔卡。
- **用得舒服。** 亮度音量调节、电量显示，蓝牙耳机/音箱音频（默认关闭，按需配对）。
- **小音效，大手感。** 移动、确认、返回、收藏，每个操作都有一声自己的音乐盒小音：同一家族、不同音符，清脆但不吵。
- **合盖不丢进度。** 合上盖子，游戏暂停、机器休眠；开盖回到刚才的地方。充电时合盖只熄屏。休眠中电量不足的话，它会定时醒来正常关机，而不是睡到一半没电。

### 上手

准备一张 8GB 以上的 microSD 卡（推荐 A1/A2）。卡比你想的重要——BootROM 对卡的初始化时序很挑剔，杂牌卡、扩容卡是黑屏的最常见原因。Samsung、Kioxia（铠侠）、SanDisk 的正品卡在这里的成功率最高。

> 刷机会清空整张卡，请先备份。

**省事的方式——[gameOS Flasher](https://github.com/oh-sudo-user/gameOS-flasher)。** Windows 图形化刷机工具（macOS 版即将推出），按机型自动匹配镜像、下载最新 release、两次 SHA-256 校验、解压、写入，全程自动；内置系统盘保护，不会误写电脑硬盘。也支持直接刷本地 `.img` / `.img.gz` 文件。

**手动方式。**

1. 从 [Releases](https://github.com/oh-sudo-user/gameOS/releases) 下载对应机型的镜像：
   - RG DS Plus → `gameOS-rgds-plus.img.gz`
   - RG DS → `gameOS-rgds.img.gz`
2. 用任意整盘镜像写入工具把镜像写进卡。[balenaEtcher](https://etcher.balena.io/) 可以直接刷 `.gz`；Linux 下可以：

   ```bash
   gunzip -c gameOS-rgds-plus.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
   ```

   把 `sdX` 换成你的卡的真实设备名（先用 `lsblk` 看清楚——这会擦掉整张卡）。是"写镜像"，不是往格式化好的卡里"拷文件"。每个镜像都附带 `.sha256` 校验文件，下载后可核对。
3. 卡插 **TF1 槽**（靠电源键的那个），开机。
4. 第一次开机会多花 10–30 秒：它要在卡的剩余空间上建数据分区，然后进入设置向导。以后就快了。
5. 拔卡插电脑，会出现一个叫 **GAMEOS-DATA** 的盘（exFAT，Windows 和 macOS 都原生读写）。把游戏放进去：

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

子文件夹随便建（最多四层）。多盘 PS1 游戏把盘放一起，用一个 `.m3u` 播放列表指过去，游戏库里就只有一条。封面可选：图片名和游戏名（去掉扩展名的文件名）一致，放进 `covers/<平台>/` 就行。

### 按键（前端界面）

主界面是一个轮播：五个平台，然后是 **Activity**、**快捷**、**设置**。左右浏览，A 打开。

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

**快捷**界面里左右切换收藏/历史两个标签；**Activity** 里左右切换概览、时间线、趋势、推荐。设置里上下选行、左右调节（亮度、音量、主题、语言）；A 进入 Wi-Fi、蓝牙、更新、存档、关机、关于等页面。游戏里各模拟器保留自己的按键和菜单——菜单语言同样跟随系统语言。

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

几个值得一提的决定：

- **没有显示服务器。** 前端和模拟器直写 DRM/KMS。少一层就少一层出错的可能，你和像素之间也不隔东西。
- **一个手柄。** Linux 把这台机器的按键、摇杆、音量键、合盖开关看成各自独立的输入设备。`gameos-inputd` 把它们合并成一个虚拟手柄——DS 机型的摇杆轴序也做了重映射——模拟器不用猜。
- **双屏开机即就绪。** 双 DSI 面板驱动直接编进内核（不是模块），没有探测竞态，不会闪。两种机型都按连接器名字选中上屏，单屏游戏也落在盖子上那块屏该在的位置。
- **A/B 启动与回滚。** 卡上有两个根文件系统槽；更新写空闲槽，引导脚本尝试新槽，四次未确认启动后自动回到旧槽。
- **默认性能模式。** CPU/GPU 满频跑，限制是散热，不是策略。

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
# 产物在 output/images/：
#   gameOS-rgds-plus.img.gz + gameOS-rgds.img.gz   （每机型一个卡镜像）
#   gameOS-rootfs.ext4.gz + gameOS-boot.tar.gz + manifest.json   （OTA 载荷）
```

所有上游组件都锁定到确切版本或 commit，同一个 commit 编出来的系统，软件版本完全一致。每次推送到 `main`，GitHub Actions 都会构建两个镜像（可作为工作流产物下载）；正式 Release 由人工从验证过的构建发布，标签构建不会覆盖已有资产的 release。

### 许可证

- gameOS 自研代码——前端、输入/休眠守护进程、构建脚本、设备补丁——采用 **GPL-3.0-or-later**。
- 集成的上游组件各自遵循其许可证：Linux（GPL-2.0）、U-Boot（GPL-2.0+）、melonDS（GPL-3.0；其内置的 Drastic FreeBIOS 为 BSD 风格许可，声明全文见 docs/TROUBLESHOOTING.md）、RetroArch（GPL-3.0）、PPSSPP（GPL-2.0+）、mGBA（MPL-2.0）、Noto 与 Orbitron 字体（OFL）。
- Rockchip DDR/BL31 固件是专有二进制，按 rkbin 仓库的许可分发。