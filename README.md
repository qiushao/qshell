
# qshell
跨平台的终端管理工具，类似 xshell/SecureCRT/mobaxterm 的终端管理工具，目前支持 linux, windows, macos 平台
支持协议：串口，ssh，local-shell；终端会话支持 XMODEM、YMODEM、ZMODEM 文件传输

已实现特性：
- 简体中文、繁体中文、英文界面，支持即时切换
- 会话管理
- 多标签
- 日记保存
- Button Bar
- Command Window
- 历史命令记录与前缀补全
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


## 界面语言
顶部菜单栏的“语言 / 語言 / Language”菜单可选择“简体中文”“繁體中文”或“English”，立即生效并在下次启动时恢复。首次启动跟随系统语言；其他系统语言默认使用英文。切换语言不会重建终端会话或清空正在输入的命令。

## Lua script engine
请参考 [LuaScriptEngine](./docs/LuaScriptEngine.md)

## 历史命令补全
终端直接输入和底部 Command Window 共享历史命令，自动保存最近 1000 条去重记录，重启后保留。输入时只按完整输入做前缀匹配（不区分大小写），在光标附近显示最多 20 条结果，按最近使用的顺序排列；不匹配中间片段或分散字符。

- 上下键选择候选，Enter 确认填入，鼠标单击也可填入。Tab 保留给 shell 自身的补全，不选择历史候选。
- 选中候选后 Enter 只填入，再按 Enter 才发送；未选择候选时 Enter 直接发送当前输入。
- Esc 关闭候选窗。Command Window 原有的 Ctrl+Up/Down 历史回看和右键历史管理仍可使用。
- 在 Command Window 中按 Ctrl+H，或右键选择 `View History...` 打开历史管理窗口。可用 Ctrl/Shift 多选，点击 `Delete Selected` 或按 Delete 删除错误记录；删除立即保存并从补全候选中移除。再次输入并执行相同命令时仍会重新记录。

历史保存在应用配置目录的 `command_history.json`，首次使用自动导入原有的 `command_history.txt`。终端根据实际回显记录命令，不记录关闭回显的密码输入、全屏应用的备用屏幕输入和串口二进制输入。终端回填依赖 shell 的行编辑支持；多行命令需要 shell 支持括号粘贴模式。

## 串口 text / bin 模式
在会话属性的串口设置中选择“数据模式 / Data Mode”，默认“文本 / Text”，旧会话保持文本模式。
“十六进制 / Hexadecimal (bin)”模式以十六进制显示接收字节，在会话底部输入框输入 `41 00 FF` 或 `4100FF`，按回车发送三个原始字节，不附加换行。发送内容也会以十六进制回显在终端中。
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
