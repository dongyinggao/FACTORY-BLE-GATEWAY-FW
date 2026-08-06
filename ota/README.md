# OTA 发布制品暂存目录

本目录仅用于本机暂存待发布的 OTA 文件，**不纳入源代码 Git 管理**。固件镜像
(`ble_gateway-<version>.bin`)、版本 Manifest (`manifest-<version>.json`、`manifest.json`)
和可选发布页面由发布流程上传到 OTA 文件服务器。

生成示例：

```bash
idf.py -B build/release -DPROJECT_VER=1.0.7 build
python3 tools/create_ota_release.py \
  --version 1.0.7 --sequence 107 \
  --image build/release/ble_gateway.bin \
  --image-url https://<ota-host>/ota/ble_gateway-1.0.7.bin \
  --output ota/manifest-1.0.7.json
```

发布前请校验镜像 SHA-256、Manifest 中的 `image_size` 和递增的
`release_sequence`，再将镜像与 Manifest 一起上传。正式步骤见
[`doc/OTA升级与发布操作说明.md`](../doc/OTA升级与发布操作说明.md)。不要将私钥、账号密码或
生产证书放入本目录。
