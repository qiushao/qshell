# How to Build qshell

# linux (ubuntu22)
## install dependencies
```shell
sudo apt install git cmake build-essential qt6-base-dev libqt6serialport6-dev qt6-tools-dev qt6-tools-dev-tools qt6-translations-l10n clang-tidy libssl-dev xauth
```

## checkout source
```shell
git clone https://github.com/qiushao/qshell.git
```

## build & install
```shell
cmake -B build -S .
cmake --build build
sudo cmake --install build
```

# windows
## 环境配置
windows 开发 qt 环境配置比较复杂，我们采用的是 msvc2022 + qt6 online installer

## build & package
在仓库根目录打开 powershell，执行 windows 的编译脚本即可
```shell
.\scripts\windows-build.ps1
```

# macos
## install dependencies
```shell
brew install cmake qt6 openssl@3
export Qt6_DIR=$(brew --prefix qt6)
```

## build & package
```shell
./scripts/macos-build.sh
```

## manual build steps
```shell
cmake -B build -S .
cmake --build build
```

## create DMG package
```shell
cd build
cpack -G DragNDrop
```

## 界面翻译

构建需要 Qt LinguistTools 和 Qt 自带的翻译文件（`qtbase_en.qm`、`qtbase_zh_CN.qm`、`qtbase_zh_TW.qm`）。翻译会编译并嵌入程序，安装后无需额外部署 `.qm` 文件。

新增界面文本后，用 Qt 6 的 `lupdate` 更新翻译目录，并补齐三种语言：

```shell
lupdate src/ui src/core src/scriptengine src/main.cpp third_party/qtermwidget -no-obsolete -locations none -ts src/resources/i18n/qshell_en.ts src/resources/i18n/qshell_zh_CN.ts src/resources/i18n/qshell_zh_TW.ts
cmake --build build
ctest --test-dir build -R i18n --output-on-failure
```

保留占位符（如 `%1`）、文件扩展名及协议标识。用户自定义的会话名、分组名、命令、路径和终端输出不参与翻译。

## SSH 与 X11

三个平台统一编译仓库内的 libssh2：Linux/macOS 使用 OpenSSL，Windows 使用 WinCNG。
SSH 连接不再依赖系统 `ssh` 或 `expect`，主机密钥确认、密码和私钥认证与 Windows 一致。
因此系统 OpenSSH 的 `~/.ssh/config`、agent、ProxyJump 等行为不再由系统客户端提供。

Linux/macOS 的 X11 转发复用同一套 libssh2 通道和事件循环，连接 `DISPLAY` 指向的本地
X server，并通过 `xauth` 读取认证 cookie（遵循 `XAUTHORITY`）。支持 `:0` 等 Unix socket、
`host:display.screen` TCP 地址和 XQuartz 的 launchd socket 路径。macOS 需安装并启动
XQuartz；应用会从 PATH 或 `/opt/X11/bin/xauth` 查找 xauth。Linux Wayland 环境需有 Xwayland。
没有可用的显示、认证或 X server 时，SSH 仍可连接，但不启用 X11 转发。

转发与 Windows 一样向远端提供本地 X server 的 cookie，仅对可信 SSH 主机使用。
它不再采用系统 `ssh -X` 的不可信 X11 授权策略。

回归测试：

```shell
ctest --test-dir build -R 'ssh_x11_pump_test|unix_x11_display_test' --output-on-failure
```
