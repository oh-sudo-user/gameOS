# OTA 清单签名（Ed25519）

gameOS 的 OTA 更新以 release 里的 `manifest.json` 为信任根：rootfs 和 boot
的 SHA-256 都写在里面。只校验收到的文件和清单里的哈希，无法防止“清单和
文件一起被替换”，所以设备端会先用内置公钥验证 `manifest.json.sig`，签名
通过后才解析清单。

## 密钥与文件

| 文件 | 位置 | 说明 |
|------|------|------|
| 私钥 | 构建机 `~/.gameos-ota-signing/ed25519.pem` | 只用于发布签名；不进仓库、不进镜像、不随 release 上传 |
| 公钥 | 仓库 `rootfs-overlay/etc/gameos/ota-pub.pem` | 编进固件，设备用它验签 |
| 签名 | release 资产 `manifest.json.sig` | 对 `manifest.json` 原始字节的 64 字节 Ed25519 签名 |

私钥丢失后，已发布设备无法再安装用新私钥签名的更新；请把
`~/.gameos-ota-signing/ed25519.pem` 备份到离线介质。私钥泄露则应轮换：
生成新密钥、替换 `ota-pub.pem`、发布一版用旧私钥签名的新固件，之后再用
新私钥签名。

## 本地构建

`post-image.sh` 在生成 `manifest.json` 后自动签名：

```sh
# 默认读取 ~/.gameos-ota-signing/ed25519.pem
make ...          # 日志出现 "manifest signed (Ed25519)"

# 或显式指定私钥
GAMEOS_OTA_SIGNING_KEY=/path/to/ed25519.pem make ...
```

找不到私钥时构建不会失败，但日志会警告 `manifest is UNSIGNED`；这种包
可以本地刷机，不能被已启用验签的设备 OTA 安装。

## 发布

```sh
# 手工补签已有 manifest
scripts/sign-ota-manifest.sh buildroot/output/images/manifest.json

# 上传时必须包含 manifest.json.sig
gh release upload <tag> buildroot/output/images/manifest.json \
  buildroot/output/images/manifest.json.sig \
  buildroot/output/images/gameOS-rootfs.ext4.gz \
  buildroot/output/images/gameOS-boot.tar.gz
```

## 设备端行为

- 下载 `manifest.json` 和 `manifest.json.sig`，签名不通过就进入
  `state=failed`，不写任何分区、不切换 `/state`。
- 签名通过后再做原有的严格字段解析、版本比较、大小/哈希检查。
- 旧版本（未签名）的 release 会被拒绝；这是有意的 fail-closed 行为。

## 测试

```sh
dash tests/shell/test-ota-signature.sh
dash tests/shell/test-gameos-update.sh
```

覆盖：有效签名、清单被改 1 字节、错误密钥、63 字节截断签名、缺失签名、
缺失公钥，以及 `do_apply` 在任何失败路径都不切换启动槽。
