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
- PSP 默认 2x 渲染分辨率；个别重负载游戏可在 PPSSPP 设置改 1x。
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
复现步骤。日志位置：`/tmp/gameos-setup.log`、`/tmp/gameos-session.log`
（可在串口 shell 中查看）。