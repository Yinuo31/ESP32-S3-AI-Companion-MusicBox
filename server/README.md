# Python WebSocket 服务

此目录由原独立服务仓库的 `server.py` 整合而来，不包含原仓库的 Git 历史。服务连接 ESP32 与火山引擎 ASR / TTS、火山方舟大模型；设备固件及硬件说明见 [项目 README](../README.md)。

## 安装

以下命令在项目根目录执行，使用 Python 3.12：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r server/requirements.txt
```

Linux 下使用 `python3 -m venv .venv`，后续 Python 路径换为 `.venv/bin/python`。

## 环境变量

| 变量 | 用途 / 默认值 |
| --- | --- |
| `ARK_API_KEY` | 必填：火山方舟 API Key |
| `ARK_BASE_URL` | 必填：账号所使用的方舟兼容接口地址 |
| `ARK_MODEL_NAME` | 模型或推理接入点名称，默认 `doubao-seed-1-8-251228`；需确认账号有访问权限 |
| `VOLC_APP_ID` | 完整语音链路必填：语音服务应用 ID |
| `VOLC_ACCESS_TOKEN` | 完整语音链路必填：语音服务访问令牌 |
| `VOLC_ASR_CLUSTER` | 默认 `volcengine_streaming_common` |
| `VOLC_TTS_VOICE` | 默认 `zh_male_yangguangqingnian_moon_bigtts`；需确认服务支持该音色 |

程序直接读取进程环境变量，**不会自动加载 `.env` 文件**。PowerShell 配置示例（请替换占位值）：

```powershell
$env:ARK_API_KEY = "YOUR_ARK_API_KEY"
$env:ARK_BASE_URL = "YOUR_ARK_BASE_URL"
$env:ARK_MODEL_NAME = "YOUR_MODEL_OR_ENDPOINT_ID"
$env:VOLC_APP_ID = "YOUR_VOLC_APP_ID"
$env:VOLC_ACCESS_TOKEN = "YOUR_VOLC_ACCESS_TOKEN"
.\.venv\Scripts\python.exe server/server.py
```

Linux 可使用 `export ARK_API_KEY='...'` 等设置，随后执行 `.venv/bin/python server/server.py`。不要将真实配置提交到 Git；终端历史也可能保存输入的凭据。

## 连接与部署

服务默认监听 `0.0.0.0:8765`。ESP32 的 `include/config.local.h` 应填写可访问的服务器地址；端口在 `include/config.h` 中配置，默认也是 `8765`。

缺少或错误的模型配置可能使客户端初始化或后续调用失败；语音链路还需要有效的语音服务凭据。服务固定使用代码中的 ASR v2 和 TTS v1 接口及 `volcano_tts` 集群，请核对自己开通的产品是否匹配。

现有服务没有设备鉴权，设备侧使用明文 WebSocket。部署前配置网络访问控制；修改文件后需要自行更新和重启部署中的服务，GitHub 推送不会自动重启它。

## 验证范围

整合时验证独立环境的依赖安装、语法、导入、监听及本地 WebSocket 控制消息交互。软件连接检查使用占位模型配置并禁止真实语音请求，不能证明真实云服务可用；语音、模型和硬件仍需有效凭据与实物验证。
