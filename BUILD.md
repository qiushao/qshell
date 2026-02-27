# How to Build qshell

# linux (ubuntu22)
## install dependencies
```shell
sudo apt install git cmake build-essential qtbase5-dev libqt5serialport5-dev clang-tidy 
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
windows 开发 qt 环境配置比较复杂，我们采用的是 msvc2022 + qt5 online installer

## build & package
在仓库根目录打开 powershell，执行 windows 的编译脚本即可
```shell
.\scripts\windows-build.ps1
```

# macos
## install dependencies
```shell
brew install cmake qt@5
export Qt5_DIR=$(brew --prefix qt@5)
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
