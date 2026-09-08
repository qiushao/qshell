
# qshell
跨平台的终端管理工具，类似 xshell/SecureCRT/mobaxterm 的终端管理工具，目前支持 linux, windows, macos 平台
支持协议：串口，ssh，local-shell；终端会话支持 XMODEM、YMODEM、ZMODEM 文件传输

已实现特性：
- 会话管理
- 多标签
- 日记保存
- Button Bar
- Command Window
- Lua script engine
- x11 转发
- XMODEM、YMODEM、ZMODEM 文件上传和下载
- 本地 MCP 终端控制接口

![screen shot](./docs/screenshot/qshell-screenshot.png)


## 安装
可以从 github release 页面下载最新版本的安装包
[qshell-release](https://github.com/qiushao/qshell/releases)

## 编译运行
请参考 [BUILD.md](./BUILD.md)


## Lua script engine
请参考 [LuaScriptEngine](./docs/LuaScriptEngine.md)

## 串口 text / bin 模式
在会话属性的串口设置中选择 `Data Mode`，默认 `text`，旧会话保持文本模式。
`bin` 模式以十六进制显示接收字节，在会话底部输入框输入 `41 00 FF` 或 `4100FF`，按回车发送三个原始字节，不附加换行。发送内容也会以十六进制回显在终端中。
每个字节必须包含两位十六进制数字；非法输入不会发送，并保留在输入框中。命令窗口也可输入十六进制字节并回车发送。
`bin` 模式不启用 XMODEM、YMODEM、ZMODEM 文件传输及自动识别。

## MCP
启用方式、Codex 配置和验证步骤请参考 [QShell MCP](./docs/MCP.md)。

## ZMODEM
使用方式和支持范围请参考 [ZMODEM 文件传输](./docs/ZMODEM.md)。

## XMODEM / YMODEM
使用方式和支持范围请参考 [XMODEM / YMODEM 文件传输](./docs/XYMODEM.md)。

## 感谢
本项目代码引用或部份参考或依赖了以下开源项目，并在此表示感谢。
- [qtermwidget](https://gitee.com/QQxiaoming/qtermwidget)
- [libssh2](https://github.com/libssh2/libssh2)
- [ptyqt](https://github.com/kafeg/ptyqt)
- [utf8proc](https://github.com/JuliaStrings/utf8proc)
- [Lua](https://www.lua.org/)
