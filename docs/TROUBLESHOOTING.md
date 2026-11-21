# gameOS 排障指南

## 1. 完全黑屏、电源灯亮

黑屏大多发生在引导早期（BootROM/SPL 阶段），多数与**存储卡**或**刷写方式**有关：

1. **换卡**：BootROM/SPL 阶段对 SD 卡初始化时序敏感。社区验证 Samsung、
   Kioxia、SanDisk 正品卡成功率最高；杂牌/扩容卡经常在此阶段失败。
2. **重刷**：用 balenaEtcher 刷 `gameOS-rgds-plus.img.gz` 整盘镜像，
   等待 Validating 校验通过。不要手动分区、不要只复制文件。
3. **强制重启**：长按电源键 10 秒关机，再开机。
4. 首次启动会扩容数据分区，屏幕可能停留 logo 10–30 秒，属正常。

## 2. 有背光但无画面 / 卡 logo

- 接串口看日志：USB-TTL 接主板调试串口，**1500000 8N1**。
  U-Boot 与内核日志都会输出（console=ttyS2,1500000n8）。
- 把完整日志贴到 Issues，注明卡的型号容量。

## 3. 进系统但按键没反应

- 确认 `/usr/bin/gameos-inputd` 在运行：串口里 `ps | grep inputd`。
- `evtest` 查看 "gameOS Gamepad" 是否有事件。
- 若只有分裂的原始设备：udev 规则未生效，重启一次。

## 4. NDS 兼容性 / 想用自己的 BIOS

NDS 开箱即玩：melonDS 核心内置 FreeBIOS（开源替代实现，非 Nintendo 文件）。
个别游戏若表现异常，可把自己主机导出的 `bios/bios7.bin`、`bios/bios9.bin`、
`bios/firmware.bin` 放进 GAMEOS 盘的 `bios/` 文件夹，存在时优先使用。

FreeBIOS 许可（Drastic BIOS replacement，随 melonDS 核心二进制分发）：

    Copyright (c) 2013, Gilead Kutnick
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1) Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2) Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

## 5. 触摸位置不对（NDS）

双屏模式下触摸已自动映射到下屏。若你用的是单屏布局核心选项，
触摸会落在下屏区域之外——请保持默认 "Top/Bottom" 布局。

## 6. 游戏卡顿

- 确认设置里亮度不是唯一调过的项：系统默认 CPU/GPU 均为 performance governor。
- PSP 默认 1x 渲染分辨率；若在 PPSSPP 里调高过，重负载游戏可改回 1x。
- PS1 默认开启 ARM64 dynarec；若某游戏异常可在 RetroArch 核心选项关闭 DRC。
- 散热：长时间游玩注意通风，RK3568 过热会降频。

## 7. 合盖后不开屏

合盖时游戏暂停、机器休眠（充电时合盖只熄屏）。开盖应自动恢复画面；
若未恢复，短按电源键唤醒一次。

## 8. 想回退 / 救砖

本机无 eMMC，**拔卡即回原状**：换回原来的卡就是原来的系统。
gameOS 不会写入机器内部存储，随便折腾。

## 9. 报告问题

提 Issue 时请附：卡型号容量、镜像 sha256、串口日志（如有）、
复现步骤。日志都在卡的 `system/logs/` 文件夹（数据分区不可用时写在
启动分区的 `logs/`，再不行才落到内存）：`setup.log`、`session.log`、
`ui.log`、`emu.log`、`retroarch.log`、`wifi.log`、`bt.log`、`tf2.log`、
`splash.log`、`inputd-supervisor.log`、`volume.log` 等，写入时会自动轮转。
卡拔下来插到电脑即可直接取走。

## 10. 界面卡顿：采集 UI 渲染性能日志

界面默认不记录任何性能数据（无额外开销）。需要排查首页/列表滑动是否
掉帧时：

1. 关机拔卡，插到电脑，在卡的 `system` 文件夹里新建一个**空文件**
   `ui-perf.on`（没有扩展名，0 字节）。
2. 装回开机，正常来回切几个界面、滑动列表 30 秒以上，再关机拔卡。
3. 读同一目录下 `logs` 文件夹里的 `ui-perf.log`。大约每 600 帧（10 秒）或切换界面时
   写一行，字段含义：
   - `screen`：这一段属于哪个界面；
   - `wall`：该帧在 UI 线程的耗时（不含 60Hz 限帧等待）的
     p50/p95/p99/max，单位毫秒；
   - `cpu`：其中真正占用 CPU 的时间，同样四档；
   - `draws`：每帧提交的 SDL 绘制调用数；
   - `dropped`：帧间隔超过 20ms（相当于掉一帧）的次数；
   - `dirty`：命令缓冲录制统计——`rec` 录制段数、`full` 全屏重绘次数、
     `skip` 画面完全没变直接跳过的帧数、`area` 平均重绘面积比例。
     未开启脏矩形时全为 0。
   想同时观察局部重绘效果，可在同一 `system` 文件夹再放一个空文件
   `ui-dirty.on`（默认关闭，仅供测试）；删掉即恢复整屏重绘。
4. 把 `ui-perf.log` 附到 Issue。排查完删掉 `ui-perf.on` 即关闭记录，
   日志文件留着不影响运行。

## 11. OTA 更新失败：manifest signature invalid

1.0.0 之后的固件只安装带有效 Ed25519 签名的 release。更新状态文件
`/run/gameos/update-state` 里出现 `manifest signature invalid` 时：

1. 确认该 release 上传了 `manifest.json.sig`，并且是对当前
   `manifest.json` 原始字节的签名（不能改一个空格后再签）。
2. 确认设备固件里的公钥与发布私钥配对；换过公钥的过渡版本要先刷入。
3. 旧 release 没有签名，设备会按设计拒绝；从第一个已签名 release 开始升级。

签名、私钥备份和发布命令见 `docs/OTA-SIGNING.md`。
