# GitHub 代码托管与 OTA 测试操作说明

本文用于将本项目推送到 GitHub，并在下周使用 GitHub Pages 完成一次受控 HTTPS OTA
测试。代码仓库为：<https://github.com/dongyinggao/FACTORY-BLE-GATEWAY-FW>。

## 1. 推送当前代码

先进入项目目录并确认当前分支和改动。不要直接向 `main` 强制推送，也不要提交 Wi-Fi、MQTT
密码、真实 SD 卡数据或私有证书。

```bash
cd /home/sm-dawn/work/project/BLEGateway
git status
git branch --show-current
git remote -v
ssh -T git@github.com
git ls-remote --heads origin
```

当前开发分支为 `main` 时，检查无误后提交并推送：

```bash
git add README.md doc main sdkconfig sdkconfig.defaults tests tools
git commit
git push -u origin main
```

提交标题使用简短的中文祈使句；提交正文统一采用以下格式：

```text
Why:
- 说明本次变更解决的问题或业务原因。

What:
- 列出主要功能或文件变更。

How:
- 简述关键实现、兼容性或验证方法。
```


建议在 GitHub 的 `Settings -> Branches` 为 `main` 设置分支保护：通过 Pull Request 合并、要求
检查通过、禁止 force push。仓库即使设为私有，也不得提交任何现场凭据。

## 2. 建立 GitHub Pages OTA 测试目录

GitHub Pages 仅用于测试，发布的固件和 Manifest 将可被持有 URL 的设备访问；不要在其中放置
配置文件、日志或密码。建议使用独立的 `gh-pages` 分支，且只保存以下文件：

```text
ota/
  index.html
  manifest-1.0.1.json
  ble_gateway-1.0.1.bin
```

在 GitHub 网页中创建 `gh-pages` 分支。该分支可从一个仅含简单 `index.html` 的初始提交开始，避免
复制完整源代码和开发资料。上传 OTA 文件后，在仓库 `Settings -> Pages` 中选择：

```text
Source: Deploy from a branch
Branch: gh-pages
Folder: /(root)
```

等待 Pages 显示已发布。测试地址固定为：

```text
https://dongyinggao.github.io/FACTORY-BLE-GATEWAY-FW/ota/manifest-1.0.1.json
https://dongyinggao.github.io/FACTORY-BLE-GATEWAY-FW/ota/ble_gateway-1.0.1.bin
```

## 3. 生成并发布首个 OTA 版本

发布镜像不能使用带 `-dirty` 的开发版本。以下示例生成版本 `1.0.1`、发布序号 `101` 的固件和
Manifest；第二行 URL 必须与 Pages 中最终固件地址完全一致。

```bash
cd /home/sm-dawn/work/project/BLEGateway
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
./tools/build_ota_release.sh 1.0.1 101 \
  https://dongyinggao.github.io/FACTORY-BLE-GATEWAY-FW/ota/ble_gateway-1.0.1.bin \
  /tmp/manifest-1.0.1.json
cp build/release/ble_gateway.bin /tmp/ble_gateway-1.0.1.bin
```

将 `/tmp/manifest-1.0.1.json` 和 `/tmp/ble_gateway-1.0.1.bin` 上传或提交到 `gh-pages` 分支的
`ota/` 目录。发布后验证服务器可访问，并且固件响应有 `Content-Length`：

```bash
curl -I https://dongyinggao.github.io/FACTORY-BLE-GATEWAY-FW/ota/manifest-1.0.1.json
curl -I https://dongyinggao.github.io/FACTORY-BLE-GATEWAY-FW/ota/ble_gateway-1.0.1.bin
```

同一个版本的 `.bin`、Manifest、GitHub Pages 路径和发布序号均不得覆盖修改。每次新版本递增：
`1.0.2 / 102`、`1.0.3 / 103`。如需测试受控降级，只能使用串口命令，不能降低已发布序号。

## 4. 网关首轮 OTA 验证

先确保测试网关正在运行已具备 OTA 功能的旧版本，且 Wi-Fi、SNTP、SD 卡、BLE 扫描均正常。MQTT
不是 OTA 前置条件，但可用于观察网关恢复后的健康状态。

```text
cfg set ota_manifest_uri https://dongyinggao.github.io/FACTORY-BLE-GATEWAY-FW/ota/manifest-1.0.1.json
cfg commit
ota check
ota status
ota start
```

`ota check` 只下载并校验 Manifest；`ota start` 还会下载镜像、校验 SHA-256、写入非运行 OTA 分区
并重启。升级会在没有活动广播、SD 可写且未满、Wi-Fi 已连接、SNTP 已同步时才开始。新固件启动后
须连续恢复 BLE 扫描和 SD 服务 10 秒，才会标记为有效；否则 Bootloader 自动回滚。

首轮测试至少记录以下结果：

1. `ota check` 显示版本、序号、硬件型号、镜像大小正确。
2. `ota start` 后重启，`ota status` 显示运行版本为 `1.0.1`。
3. BLE 扫描、CSV 写入、Wi-Fi、MQTT 恢复正常。
4. 使用错误 SHA-256、错误目标型号或低发布序号的 Manifest，确认升级被拒绝。
5. 在新固件首次启动确认前断电或重启，确认设备自动回滚。

## 5. 后续生产服务迁移

GitHub Pages 适合作为公开 HTTPS 测试文件服务，不应作为长期厂内发布平台。生产阶段建议迁移到
自家 NAS 或服务器的独立域名，例如 `https://ota.company.example/`，并满足：公共 CA 签发的 HTTPS
证书、静态文件、稳定的 `Content-Length`、访问权限和发布审计。当前固件不接受裸 IP 地址、自签名
证书或需要网页登录/Basic Auth 的 OTA 地址；如需厂内私有 CA，必须先在固件中加入对应根证书并完成
回归验证。

完整 OTA 安全机制、Manifest 字段和回滚规则见
[OTA升级与发布操作说明](OTA升级与发布操作说明.md)。
