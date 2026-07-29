# ZMODEM 文件传输

QShell 会自动识别终端数据流中的 ZMODEM 握手，不需要在本机安装 `lrzsz`。

## 上传到远端

1. 在远端终端执行 `rz`。
2. QShell 弹出文件选择窗口后，可选择一个或多个文件。
3. 传输窗口显示当前文件进度、传输速率和预计剩余时间；点击“取消”会同时通知远端终止传输。

## 从远端下载

1. 在远端终端执行 `sz 文件名`，也可以一次指定多个文件。
2. QShell 弹出目录选择窗口，选择本地保存目录。
3. 如果同名文件已经存在，QShell 会生成带编号的新文件名，不覆盖原文件。

下载先写入同目录的临时文件，完整接收并通过 CRC 校验后再提交为目标文件。中断或失败不会留下不完整的目标文件。

## Lua 与 MCP 自动化

Lua 脚本和 MCP 可以在远端 ZMODEM 命令执行前预设本地路径。握手出现后，
QShell 会直接使用预设值，不打开文件或目录选择窗口。上传可预设一个或多个
本地文件；下载接收路径是一个已经存在且可写的目录。每个预设只用于下一次
方向匹配的传输。

Lua：

```lua
assert(qshell.zmodem.download("/tmp/downloads"))
qshell.screen.sendText("sz /var/log/app.log\r")
```

MCP：

```json
{"name":"qshell_zmodem_download","arguments":{"directoryPath":"/tmp/downloads"}}
{"name":"qshell_send_text","arguments":{"text":"sz /var/log/app.log\\r"}}
```

完整接口说明见 [Lua 脚本 API](./LuaScriptEngine.md) 和
[MCP 工具列表](./MCP.md)。

## 支持范围

- 支持十六进制、CRC16 二进制和 CRC32 二进制帧。
- 支持批量上传和下载、ZDLE 转义、CRC 错误重传及用户取消。
- 支持 SSH、串口和本地终端会话。
- 标准 ZMODEM 使用 32 位文件偏移，因此单个文件不能超过 4 GiB。
- 不支持 `ZCOMMAND` 远程命令、文本换行转换和跨会话断点续传。
