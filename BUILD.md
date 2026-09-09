# How to Build qshell

# linux (ubuntu22)
## install dependencies
```shell
sudo apt install git cmake build-essential qt6-base-dev libqt6serialport6-dev qt6-tools-dev qt6-tools-dev-tools qt6-translations-l10n clang-tidy
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
brew install cmake qt6
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
