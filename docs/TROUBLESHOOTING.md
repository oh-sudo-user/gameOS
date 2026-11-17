# gameOS 排障指南

## 1. 完全黑屏、电源灯亮

这是 BananaOS issue #183 的典型症状，多数与**存储卡**或**刷写方式**有关：

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

## 4. NDS 启动报错 / 提示缺 BIOS

NDS 需要你自己的 BIOS 文件（版权原因不内置）：
`bios/bios7.bin`、`bios/bios9.bin`、`bios/firmware.bin`。
主界面会列出缺少的文件名。放好后重新进入游戏列表即可。

## 5. 触摸位置不对（NDS）

双屏模式下触摸已自动映射到下屏。若你用的是单屏布局核心选项，
触摸会落在下屏区域之外——请保持默认 "Top/Bottom" 布局。

## 6. 游戏卡顿

- 确认设置里亮度不是唯一调过的项：系统默认 CPU/GPU 均为 performance governor。
- PSP 默认 2x 渲染分辨率；个别重负载游戏可在 PPSSPP 设置改 1x。
- PS1 默认开启 ARM64 dynarec；若某游戏异常可在 RetroArch 核心选项关闭 DRC。
- 散热：长时间游玩注意通风，RK3568 过热会降频。

## 7. 合盖后不开屏

合盖只熄屏、不休眠。开盖应自动恢复背光；若未恢复，
短按电源键或任意键唤醒一次（输入守护进程会恢复背光值）。

## 8. 想回退 / 救砖

本机无 eMMC，**拔卡即回原状**：换回原来的卡就是原来的系统。
gameOS 不会写入机器内部存储，随便折腾。

## 9. 报告问题

提 Issue 时请附：卡型号容量、镜像 sha256、串口日志（如有）、
复现步骤。日志位置：`/tmp/gameos-setup.log`、`/tmp/gameos-session.log`
（可在串口 shell 中查看）。