# MyQueue 编译安装指南

## 编译环境要求

- **操作系统**: Linux (已测试: Ubuntu 20.04+, CentOS 7+)
- **编译器**: GCC 8.0+ 或 Clang 7.0+ (需支持 C++17)
- **CMake**: 3.14+
- **Git**: 用于下载依赖

### 检查编译器版本

```bash
g++ --version
cmake --version
```

## 编译选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_STATIC` | OFF | 静态链接 C++ 运行时 (推荐用于分发) |
| `BUILD_FULLY_STATIC` | OFF | 完全静态编译 (无动态库依赖) |
| `BUILD_TESTS` | ON | 编译测试程序 |
| `CMAKE_BUILD_TYPE` | Release | 编译类型 (Debug/Release) |

## 编译方式

### 1. 标准编译 (开发使用)

```bash
mkdir build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

生成的可执行文件: `build/myqueue`

### 2. 静态编译 (推荐分发)

静态链接 C++ 运行时库，动态链接 glibc。适用于大多数 Linux 系统。

```bash
mkdir build_static
cmake -S . -B build_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_STATIC=ON \
    -DBUILD_TESTS=OFF
cmake --build build_static -j$(nproc)
```

生成的可执行文件: `build_static/myqueue`

### 3. 完全静态编译 (跨系统分发)

完全静态链接，无任何动态库依赖。可在任意 Linux 系统运行，不受 glibc 版本限制。

```bash
mkdir build_full_static
cmake -S . -B build_full_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_FULLY_STATIC=ON \
    -DBUILD_TESTS=OFF
cmake --build build_full_static -j$(nproc)
```

生成的可执行文件: `build_full_static/myqueue`

验证静态编译:
```bash
file build_full_static/myqueue
# 输出应包含 "statically linked"

ldd build_full_static/myqueue
# 输出应为 "不是动态可执行文件" 或 "not a dynamic executable"
```

## 在不同 GCC 版本下编译

本项目支持 GCC 8.0 及以上版本。

### GCC 8.x (如 CentOS 7/8 集群)

```bash
# 如果系统默认 GCC 版本较低，需要先启用高版本 GCC
# CentOS 7:
scl enable devtoolset-8 bash

# 或者指定编译器
cmake -S . -B build \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++-8 \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_FULLY_STATIC=ON \
    -DBUILD_TESTS=OFF
cmake --build build -j$(nproc)
```

### GCC 9+ (Ubuntu 20.04+)

直接使用默认编译器即可:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_FULLY_STATIC=ON -DBUILD_TESTS=OFF
cmake --build build -j$(nproc)
```

## 编译测试程序

```bash
mkdir build_test
cmake -S . -B build_test -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build_test -j$(nproc)

# 运行测试
cd build_test && ctest --output-on-failure
```

## 安装

```bash
# 安装到 /usr/local/bin (需要 root 权限)
sudo cmake --install build

# 或者安装到自定义目录
cmake --install build --prefix ~/.local

# 或者直接复制可执行文件
cp build/myqueue ~/bin/
```

## 常见问题

### Q: 编译时报错 "filesystem" 相关错误

GCC 8.x 需要显式链接 `stdc++fs` 库，CMakeLists.txt 已自动处理。如果仍有问题:
```bash
# 确保安装了完整的 GCC
sudo apt install g++-8 libstdc++-8-dev  # Ubuntu/Debian
sudo yum install gcc-c++ libstdc++-devel  # CentOS/RHEL
```

### Q: 完全静态编译后出现 getpwuid 警告

这是正常的。glibc 的 `getpwuid` 函数在静态链接时有 NSS 限制，但不影响程序核心功能。程序会优先使用环境变量 `$USER` 和 `$HOME`。

### Q: 如何在旧系统上运行?

使用 `BUILD_FULLY_STATIC=ON` 编译的二进制文件可以直接复制到任何 x86_64 Linux 系统运行，无需安装任何依赖。

### Q: 编译时下载依赖失败

程序依赖 Google Test 和 RapidCheck (仅测试需要)。如果网络问题导致下载失败:
```bash
# 跳过测试编译
cmake -S . -B build -DBUILD_TESTS=OFF
```

## 目录结构

```
myqueue/
├── include/myqueue/    # 头文件
├── src/
│   ├── cli/           # 命令行入口
│   ├── core/          # 核心功能
│   └── ipc/           # 进程间通信
├── tests/             # 测试文件
├── CMakeLists.txt     # 构建配置
├── README.md          # 项目说明
└── INSTALL.md         # 本文件
```
