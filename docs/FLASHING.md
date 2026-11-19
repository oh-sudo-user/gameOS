# gameOS 刷写指南（Anbernic RG DS Plus）

## 准备

- 一张 **16GB 或更大** 的 microSD 卡。推荐 Samsung / Kioxia（铠侠）/ SanDisk
  正品卡；杂牌卡是黑屏不开机的常见原因之一（BootROM 阶段对 SD 卡时序敏感）。
- 一台电脑（Windows / macOS / Linux 均可）。
- 刷机工具 [balenaEtcher](https://etcher.balena.io/)（免费）。
- 镜像文件 `gameOS-rgds-plus.img.gz`（从 Releases 页面或 CI 产物下载）。
  **不需要解压**，Etcher 可以直接刷 .gz。

> 刷机会清空整张卡。卡里若有旧资料请先备份。

## 刷写步骤

1. 把 microSD 卡通过读卡器插入电脑。
2. 打开 balenaEtcher：
   - **Flash from file** → 选择 `gameOS-rgds-plus.img.gz`
   - **Select target** → 选中你的 microSD 卡（看清容量，别选错盘！）
   - **Flash!** → 等待写入 + 校验完成。
3. 校验通过后拔卡，插入 RG DS Plus **下屏一侧的 TF 卡槽**。
4. 长按电源键开机。首次启动会自动扩容数据分区，约需 10–30 秒，
   之后进入 gameOS 主界面（上屏轮播、下屏时钟）。

## 放入游戏和 BIOS

把卡插回电脑，会出现一个名为 **GAMEOS** 的 exFAT 盘符（Windows 可直接读写）：

```
GAMEOS/
├── roms/
│   ├── nds/    ← .nds 游戏
│   ├── gba/    ← .gba 游戏
│   ├── gbc/    ← .gb / .gbc 游戏
│   ├── ps1/    ← .bin/.cue / .chd / .pbp
│   └── psp/    ← .iso / .cso
├── bios/       ← 可选：自己的 NDS BIOS（不放也能玩）
├── saves/      ← 存档（自动）
└── screenshots/
```

- **NDS 开箱即玩**：核心自带开源 FreeBIOS，不放 BIOS 也能玩；把自己导出的
  三个文件放进 `bios/` 可获得最佳兼容性（存在时优先使用）。
- PS1 推荐 `.chd` 或 `.pbp` 格式（单文件、省空间）；`.bin/.cue` 也可。
- PSP 游戏无需 BIOS。

## 操作方式

| 按键 | 功能 |
|------|------|
| 右键（A 位置） | 确认 / 启动游戏 |
| 下键（B 位置） | 返回 |
| 方向键 / 摇杆 | 选择 |
| START | 主界面 ↔ 设置 |
| SELECT | 回到主界面 |
| 游戏中 SELECT+START | 退出游戏回到 gameOS |
| 游戏中 SELECT+X | RetroArch 快捷菜单 |
| 游戏中 SELECT+L / R | 即时存档 / 读档 |
| 音量键 | 系统音量 |
| 合盖 | 熄屏省电（开盖恢复，不会休眠死机） |

设置里可调亮度、音量、连线 WiFi、检查系统更新、关机、重启。

## 系统更新（OTA，无需电脑）

系统采用 A/B 双分区：当前系统在 A 区运行，更新包写入空闲的 B 区，
重启后从 B 区启动；若新系统多次启动失败，U-Boot 会自动回退到原来的 A 区。

1. 设置 → **WiFi**，连上家里的无线网络。
2. 设置 → **系统更新**，按 Ⓐ 检查（连网后开机也会自动检查一次）。
   只有 GitHub 上最新 Release 的版本号**比本机新**才会提示「发现新版本」；
   一样或更旧都显示「已是最新」，不会重复安装。
3. 按 Ⓐ 后先弹出「确认更新到 x.x.x？」，再按 Ⓐ 确认才开始下载并写入空闲分区
   （Ⓑ 取消）。下载中会显示百分比和校验结果。
4. 进度条走完显示「重启生效」后，按 Ⓐ 重启即进入新系统。

更新只改写空闲分区，正在运行的系统始终完整；即使更新中断，
下次开机仍回到原来的版本。数据分区（游戏、存档）不受影响。

## 刷完不开机？

按顺序排查（详见 [TROUBLESHOOTING.md](TROUBLESHOOTING.md)）：

1. 换一张 Samsung / Kioxia 正品卡重刷（最常见原因）。
2. 确认刷的是 `.img.gz` 整盘镜像，而不是把文件复制进卡里。
3. 确认 Etcher 校验（Validating）通过。
4. 长按电源键 10 秒强制关机后再开。
5. 仍不行：接 USB-TTL 串口（1500000 波特率）看启动日志，
   把日志发到仓库 Issues。