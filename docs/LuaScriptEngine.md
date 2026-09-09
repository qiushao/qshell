# QShell Lua 脚本使用文档

## 概述

QShell 提供了内置的 Lua 脚本引擎，用于终端会话的自动化操作。所有 API 都通过 `qshell` 命名空间访问。
Lua 的语法细节请参考 [lua-tutorial](https://www.runoob.com/lua/lua-tutorial.html)

---

## API 参考

### 1. qshell 模块

#### `qshell.showMessage(msg)`
弹出消息对话框。

| 参数 | 类型 | 说明 |
|------|------|------|
| `msg` | string | 要显示的消息内容 |

example:
```lua
qshell.showMessage("操作完成！")
```

---

#### `qshell.input(prompt, [defaultValue], [title])`
弹出输入对话框，获取用户输入。

| 参数 | 类型 | 说明 |
|------|------|------|
| `prompt` | string | 提示信息 |
| `defaultValue` | string (可选) | 默认值 |
| `title` | string (可选) | 对话框标题，默认为 "Script Input" |

**返回值**: `string` - 用户输入的内容，取消时返回空字符串

example:
```lua
local username = qshell.input("请输入用户名:", "admin", "登录")
if username ~= "" then
    qshell.log("用户名: " .. username)
end
```

---

#### `qshell.log(msg)`
输出调试日志到 Qt 调试控制台。调用 `qshell.setLogFile(path)` 后，同时将原始消息以 UTF-8
追加到文件，每次调用在末尾添加换行并刷新文件。写入失败会抛出 Lua 错误，可用 `pcall` 捕获。

example:
```lua
qshell.log("脚本开始执行...")
```

---

#### `qshell.setLogFile(path, [append])`
设置当前脚本的日志文件，后续 `qshell.log(msg)` 同时输出到该文件和 Qt 调试控制台。

- `path` 为文件路径；相对路径基于应用进程的当前工作目录。
- `append` 为可选布尔值，默认 `true`，追加日志；传 `false` 时在调用本接口时清空已有文件，后续日志依次写入。
- 文件不存在时创建；父目录必须已存在。
- 传入空字符串 `""` 关闭文件日志；切换路径会关闭之前的文件。
- 正常结束、出错或停止脚本后自动关闭文件，下次运行需重新设置。
- 成功无返回值；打开失败会抛出 Lua 错误，保留之前的日志文件设置。
- 仅记录 `qshell.log` 消息，不包含 Lua `print` 或应用的其他调试输出。

example:
```lua
qshell.setLogFile("script.log")        -- 默认追加
qshell.setLogFile("script.log", true)  -- 显式追加
qshell.setLogFile("script.log", false) -- 清空已有日志，重新记录
qshell.log("脚本开始执行...")
qshell.log("操作完成")
qshell.setLogFile("")
```

---

#### `qshell.sleep(seconds)`
暂停脚本执行指定秒数。

| 参数 | 类型 | 说明 |
|------|------|------|
| `seconds` | number | 等待的秒数 |

example:
```lua
qshell.sleep(2)  -- 等待 2 秒
```

---

#### `qshell.getVersionStr()`
获取应用程序版本号。

**返回值**: `string` - 版本号字符串

example:
```lua
local version = qshell.getVersionStr()
qshell.log("当前版本: " .. version)
```

---

### 2. 屏幕模块 (`qshell.screen`)

#### `qshell.screen.sendText(text)`
向当前终端发送文本，不会自动添加换行，可以使用 `\r` 表示换行。

example:
```lua
qshell.screen.sendText("ls -la\r")
```

---

#### `qshell.screen.sendBinary(data)`
串口 `bin` 模式使用。向当前终端发送单个字节或字节数组，不附加回车或换行。 会将发送字节以十六进制回显到终端。

**参数**: `data` 为 `0–255` 的整数，或由这些整数组成的 Lua 数组（从索引 1 开始连续排列）。数组按索引顺序发送，每个整数对应一个字节。

**返回值**: `boolean` - 已向当前连接的后端提交数据时为 `true`，不代表设备已确认收到。
参数类型错误、数值越界、包含小数或数组索引不连续时返回 `false`，不发送任何字节。
没有当前会话、会话未连接或正在进行/等待文件传输时也返回 `false`。
空数组在允许发送时返回 `true`，不发送数据、不产生回显。

```lua
qshell.screen.sendBinary(0xC0)             -- 发送 C0
qshell.screen.sendBinary({0x41, 0x00, 0xFF}) -- 发送 41 00 FF
```

---

#### `qshell.screen.sendKey(keyName)`
发送特殊按键。

**支持的按键**:
| 按键类型 | 值 |
|---------|-----|
| 功能键 | `Enter`, `Tab`, `Escape`, `Backspace`, `Delete` |
| 方向键 | `Up`, `Down`, `Left`, `Right` |
| 导航键 | `Home`, `End`, `PageUp`, `PageDown` |
| F键 | `F1` - `F12` |
| 组合键 | `Ctrl+C`, `Ctrl+D` 等 |

example:
```lua
qshell.screen.sendText("ping 192.168.1.168\r")
qshell.sleep(3)
qshell.screen.sendKey("Ctrl+C")
```

---

#### `qshell.screen.getScreenText()`
获取当前屏幕的全部文本内容。

**返回值**: `string` - 屏幕文本

example:
```lua
qshell.screen.sendText("ifconfig\r")
qshell.sleep(2)
local str = qshell.screen.getScreenText()
local ip = str:match("eth0.-inet addr:([%d%.]+)")
if (ip == false or ip == nil) then
   qshell.showMessage("get ip failed")
   return
end
```

---

#### `qshell.screen.containString(str)`
判断当前屏幕内容是否包含指定字符串。

| 参数 | 类型 | 说明 |
|------|------|------|
| `str` | string | 要查找的字符串 |

**返回值**: `boolean` - `true` 表示包含，`false` 表示不包含

example:
```lua
if qshell.screen.containString("login:") then
    qshell.log("发现登录提示")
end
```

---

#### `qshell.screen.clear()`
清除当前终端屏幕。

example:
```lua
qshell.screen.clear()
```

---

#### `qshell.screen.waitForString(str, timeoutSeconds)`
等待屏幕上出现指定字符串。

| 参数 | 类型 | 说明 |
|------|------|------|
| `str` | string | 要等待的字符串 |
| `timeoutSeconds` | number | 超时时间（秒） |

**返回值**: `boolean` - `true` 表示找到，`false` 表示超时

example:
```lua
qshell.screen.sendText("ssh user@server\r")

if qshell.screen.waitForString("password:", 30) then
    qshell.screen.sendText("mypassword\r")
else
    qshell.showMessage("连接超时！")
end
```

---

#### `qshell.screen.waitForRegexp(pattern, timeoutSeconds)`
等待屏幕上出现匹配正则表达式的内容。

| 参数 | 类型 | 说明 |
|------|------|------|
| `pattern` | string | 正则表达式模式 |
| `timeoutSeconds` | number | 超时时间（秒） |

**返回值**: `boolean` - `true` 表示匹配成功，`false` 表示超时

example:
```lua
qshell.screen.sendText("reboot\r")
qshell.sleep(3)
-- 等待 shell 提示符 ($ 或 #)
if qshell.screen.waitForRegexp("[\\$#]\\s*$", 10) then
    qshell.log("已就绪")
end
```

---

#### `qshell.screen.getLastMatch()`
获取最后一次正则匹配的内容。

**返回值**: `string` - 匹配的字符串

example:
```lua
if qshell.screen.waitForRegexp("IP: ([0-9.]+)", 5) then
    local match = qshell.screen.getLastMatch()
    qshell.log("匹配到: " .. match)
end
```

---

### 3. 会话模块 (`qshell.session`)

#### `qshell.session.open(sessionName)`
通过名称打开已保存的会话。

| 参数 | 类型 | 说明 |
|------|------|------|
| `sessionName` | string | 会话名称 |

**返回值**: `boolean` - 是否成功打开

example:
```lua
if qshell.session.open("MyServer") then
    qshell.log("会话已打开")
end
```

---

#### `qshell.session.tabName()`
获取当前标签页的会话名称。

**返回值**: `string` - 会话名称

example:
```lua
local name = qshell.session.tabName()
qshell.log("当前会话: " .. name)
```

---

#### `qshell.session.nextTab()`
切换到下一个标签页。

example:
```lua
qshell.session.nextTab()
```

---

#### `qshell.session.switchToTab(tabName)`
切换到指定名称的标签页。

| 参数 | 类型 | 说明 |
|------|------|------|
| `tabName` | string | 标签页名称 |

**返回值**: `boolean` - 是否成功切换

example:
```lua
if qshell.session.switchToTab("Server1") then
    qshell.log("已切换到 Server1")
end
```

---

#### `qshell.session.connect()`
连接当前会话。

example:
```lua
qshell.session.connect()
```

---

#### `qshell.session.disconnect()`
断开当前会话连接。

example:
```lua
qshell.session.disconnect()
```

---

### 4. ZMODEM 模块 (`qshell.zmodem`)

ZMODEM 接口用于预设下一次传输的本地文件或接收目录。必须先调用
ZMODEM 接口，再通过终端执行远端的 `rz` 或 `sz` 命令。预设只使用一次，
传输握手出现后不会弹出文件或目录选择窗口。

#### `qshell.zmodem.upload(filePaths)`

为下一次远端 `rz` 预设一个本地文件，或由字符串组成的本地文件列表。

**返回值**: `boolean` - 当前会话已连接、文件均可读且预设成功时为 `true`

example:
```lua
assert(qshell.zmodem.upload({
    "/tmp/report.txt",
    "/tmp/result.bin"
}))
qshell.screen.sendText("rz\r")
```

---

#### `qshell.zmodem.download(directoryPath)`

为下一次远端 `sz` 预设本地接收目录。目录必须已经存在并且可写；远端文件名
保持不变，同名文件仍按现有 ZMODEM 规则生成带编号的新文件名。

**返回值**: `boolean` - 当前会话已连接、目录可写且预设成功时为 `true`

example:
```lua
assert(qshell.zmodem.download("/tmp/downloads"))
qshell.screen.sendText("sz /var/log/app.log\r")
```

---

### 5. 定时器模块 (`qshell.timer`)

#### `qshell.timer.setTimeout(callback, delayMs)`
创建单次定时器，在指定延迟后执行回调函数。

| 参数 | 类型 | 说明 |
|------|------|------|
| `callback` | function | 定时器触发时执行的回调函数 |
| `delayMs` | number | 延迟时间（毫秒） |

**返回值**: `number` - 定时器 ID，可用于取消定时器

example:
```lua
local id = qshell.timer.setTimeout(function()
    qshell.log("3秒后执行！")
end, 3000)
```

---

#### `qshell.timer.setInterval(callback, intervalMs)`
创建重复定时器，按指定间隔周期性执行回调函数。

| 参数 | 类型 | 说明 |
|------|------|------|
| `callback` | function | 定时器触发时执行的回调函数 |
| `intervalMs` | number | 间隔时间（毫秒） |

**返回值**: `number` - 定时器 ID，可用于取消定时器

example:
```lua
local count = 0
local id = qshell.timer.setInterval(function()
    count = count + 1
    qshell.log("心跳检测 #" .. count)
end, 5000)

-- 稍后取消
-- qshell.timer.clear(id)
```

---

#### `qshell.timer.clear(timerId)`
取消指定的定时器。

| 参数 | 类型 | 说明 |
|------|------|------|
| `timerId` | number | 要取消的定时器 ID |

**返回值**: `boolean` - `true` 表示成功取消，`false` 表示未找到该定时器

example:
```lua
local id = qshell.timer.setTimeout(function()
    qshell.log("这条消息不会显示")
end, 5000)

-- 在定时器触发前取消
qshell.timer.clear(id)
```

---

#### `qshell.timer.clearAll()`
取消所有活动的定时器。

example:
```lua
-- 清理所有定时器
qshell.timer.clearAll()
qshell.log("所有定时器已清除")
```

---

#### `qshell.timer.process()`
手动处理所有到期的定时器回调。在长时间运行的循环中应定期调用此函数，以确保定时器能够正常触发。
一般来说长时间运行的循环中，都会有 qshell.sleep 的调用，不然 cpu 就抗不住了，
如果有调用了 qshell.sleep(seconds) 接口，就没必要再调用 qshell.timer.process()
因为在 qshell.sleep 内部会有触发定时器的逻辑

example:
```lua
-- 在自定义循环中处理定时器
while running do
    -- 执行其他操作...
    
    -- 处理到期的定时器
    qshell.timer.process()
    
end
```

---

#### `qshell.timer.count()`
获取当前活动定时器的数量。

**返回值**: `number` - 活动定时器数量

example:
```lua
local n = qshell.timer.count()
qshell.log("当前有 " .. n .. " 个活动定时器")
```


### 6. 串口模块 (`qshell.serial`)

直接按设备路径打开串口，可同时操作多个设备，不依赖当前终端标签页。
同一设备若已被终端会话或其他程序占用，需先断开对应连接。

#### `qshell.serial.open(path, [options])`

返回串口对象，后续使用冒号调用方法。打开或配置失败会抛出 Lua 错误，
可以使用 `pcall` 捕获。串口在脚本正常结束、报错或停止时自动关闭；也可主动 `close()`。
脚本结束后保留在全局变量中的串口对象已关闭，下一次运行需要重新打开。

| 选项 | 默认值 | 可选值 |
|------|--------|--------|
| `baudRate` | `115200` | 设备支持的正整数波特率 |
| `dataBits` | `8` | `5`、`6`、`7`、`8` |
| `parity` | `"none"` | `"none"`、`"even"`、`"odd"`、`"space"`、`"mark"` |
| `stopBits` | `1` | `1`、`1.5`、`2`（取决于平台/设备支持） |
| `flowControl` | `"none"` | `"none"`、`"hardware"`、`"software"` |

```lua
local ok, port = pcall(qshell.serial.open, "/dev/ttyUSB2", {baudRate = 115200})
if not ok then
    qshell.log(port) -- 错误原因
    return
end
port:writeText("echo hello\r")
qshell.log(port:readText(4096, 1000))
port:close()
```

#### `port:writeText(text, [timeoutMs])`

原样发送 Lua 字符串，不附加换行、不转换编码，保留内嵌 `\0`。
UTF-8 文本可直接传入；其他编码需要调用方自行处理。
返回写出的字节数，空字符串返回 `0`。`timeoutMs` 默认为 `1000` 毫秒，必须非负。
成功表示数据已交给系统串口驱动，不代表设备已经接收或执行命令。
写入超时、失败或中断时抛出错误并关闭端口，避免残留待发数据；部分数据可能已经发出。

#### `port:writeBinary(data, [timeoutMs])`

发送原始字节，返回值和超时规则与 `writeText` 相同。`data` 支持：

- 单个 `0..255` 整数，如 `0xC0`。
- 从索引 1 开始连续排列的整数数组，如 `{0x41, 0x00, 0xFF}`。
- Lua 二进制字符串，如 `string.char(0x41, 0x00, 0xFF)`。

数组包含非整数、越界值或不连续索引时抛出错误，整次调用不发送任何字节。
空数组返回 `0`。字符串 `"C0"` 发送的是两个 ASCII 字节，发送单字节命令应使用 `0xC0`。

#### `port:readText([maxBytes], [timeoutMs])`
#### `port:readBinary([maxBytes], [timeoutMs])`

最多读取 `maxBytes` 个字节（默认 `4096`，必须为正整数），等待首批数据最多
`timeoutMs` 毫秒（默认 `1000`，必须非负；`0` 表示立即检查）。有数据即返回，
不等待填满长度、不按行或协议帧分割；未取出的字节保留给下一次读取。
文本跨多次读取时可能拆分 UTF-8 字符，先拼接完整数据再处理。

`readText` 返回原始 Lua 字符串，`readBinary` 返回 `0..255` 整数数组。
超时无数据分别返回 `""` 和 `{}`；端口关闭、设备错误或用户停止脚本会抛出错误。
两种读取方式共享接收缓冲区，数据只消费一次。读写等待可响应停止请求；
等待期间不执行 Lua 定时器回调，需要时在两次调用间使用 `qshell.timer.process()`。

#### `port:isOpen()` / `port:close()`

`isOpen()` 返回端口是否打开。`close()` 释放端口，可重复调用。

#### Android 开发板与红外控制器

```lua
local board = qshell.serial.open("/dev/ttyUSB2", {baudRate = 115200})
local ir = qshell.serial.open("/dev/ttyUSB0", {baudRate = 115200})
board:writeText("echo hello\r")
qshell.log(board:readText(4096, 1000))
ir:writeBinary(0xC0) -- 发送 power 红外码
ir:close()
board:close()
```

两个波特率需要按实际设备设置。完整示例见 `scripts/lua/serial_devices.lua`：

```bash
qshell --script scripts/lua/serial_devices.lua -- /dev/ttyUSB2 115200 /dev/ttyUSB0 115200
```

## 完整示例

### 示例 1： reboot 压测
打开两个会话，一个串口，一个本地 shell，
串口用于控制板子，local shell 用于 adb 连接，拉取日志

```lua
-- 使用 qshell.app 模块
versionInfo = qshell.getVersionStr()
qshell.log("脚本开始执行..." .. versionInfo)

ret = qshell.session.open("ttyUSB1")
if (ret == false) then
    qshell.showMessage("open session ttyUSB1 failed")
    return
end
qshell.sleep(1)
qshell.screen.sendText("su\r")
qshell.screen.sendText("setprop persist.auto.logd.enable 1\r")

ret = qshell.session.open("bash")
if (ret == false) then
    qshell.showMessage("open session bash failed")
    return
end

qshell.sleep(1)
qshell.screen.sendText("mkdir -p ~/reboot_log\r")
qshell.screen.sendText("cd ~/reboot_log\r")

for i = 1, 10 do
    ret = qshell.session.switchToTab("ttyUSB1");
    if (ret == false) then
        qshell.showMessage("switchToTab ttyUSB1 failed")
        return
    end
    qshell.sleep(1)

    qshell.screen.sendText("reboot\r")
    qshell.sleep(3)
    ret = qshell.screen.waitForString("console:/ $", 30)
    if (ret == false) then
        qshell.showMessage("wait console start failed")
        return
    end

    qshell.sleep(1)
    qshell.screen.sendText("\r")
    qshell.screen.sendText("su\r")
    qshell.sleep(1)
    qshell.screen.sendText("echo 0 > /proc/sys/kernel/printk\r")
    qshell.sleep(1)
    qshell.screen.sendText("start adbd\r")
    qshell.sleep(1)
    qshell.screen.sendText("logcat > /data/reboot" .. i .. ".log&\r")
    qshell.sleep(1)
    qshell.screen.sendText("dmesg -w > /data/dmesg" .. i .. ".log&\r")
    qshell.sleep(30)
    qshell.screen.sendText("fg\r")
    qshell.sleep(1)
    qshell.screen.sendKey("Ctrl+C")
    qshell.sleep(1)
    qshell.screen.sendText("fg\r")
    qshell.sleep(1)
    qshell.screen.sendKey("Ctrl+C")
    qshell.sleep(1)

    qshell.screen.sendText("ifconfig\r")
    qshell.sleep(2)
    local str = qshell.screen.getScreenText()
    local ip = str:match("eth0.-inet addr:([%d%.]+)")
    if (ip == false or ip == nil) then
        qshell.showMessage("get ip failed")
        return
    end

    ret = qshell.session.switchToTab("bash")
    if (ret == false) then
        qshell.showMessage("switchToTab bash failed")
        return
    end
    qshell.sleep(1)

    qshell.screen.sendText("\r")
    qshell.sleep(1)
    qshell.screen.sendText("adb connect " .. ip .. "\r")
    qshell.sleep(2)
    qshell.screen.sendText("adb root\r")
    qshell.sleep(1)
    qshell.screen.sendText("adb pull /data/reboot" .. i .. ".log ~/reboot_log/\r")
    qshell.sleep(1)
    qshell.screen.sendText("adb pull /data/dmesg" .. i .. ".log ~/reboot_log/\r")
    qshell.sleep(1)
    qshell.screen.sendText("adb disconnect\r")
    qshell.sleep(2)
end

qshell.showMessage("exec script finish")
```
