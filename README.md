# MyQueue

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](https://www.linux.org/)

**MyQueue** 是一个用户级 GPU/CPU 计算任务队列管理系统，专为高性能计算集群设计。无需 root 权限，支持智能资源调度、CPU-GPU 亲和性绑定、批量任务提交等功能。

## ✨ 核心特性

### 🚀 智能调度
- **自动资源检测**: 实时监控 GPU 显存使用和 CPU 利用率
- **CPU-GPU 亲和性**: 自动绑定 NUMA 节点，优化数据传输性能
  - GPU 0-3 ↔ CPU 0-31 (NUMA Node 0)
  - GPU 4-7 ↔ CPU 32-63 (NUMA Node 1)
- **FIFO 调度**: 先进先出，公平分配资源
- **资源隔离**: 每个任务独立的 CPU/GPU 资源，互不干扰

### 💼 任务管理
- **后台守护进程**: 服务持久化运行，任务自动调度
- **批量提交**: 支持通过工作目录列表批量提交任务
- **任务监控**: 实时查看任务状态、运行时长、资源占用
- **日志管理**: 任务日志自动输出到工作目录
- **进程树管理**: 正确终止任务及其所有子进程（如 mpirun, vasp）

### 🔧 易用性
- **单一可执行文件**: 无需安装，复制即用
- **完全静态编译**: 跨 Linux 系统运行，无依赖
- **彩色终端输出**: 直观的任务状态显示
- **用户级运行**: 无需 root 权限，多用户隔离

## 📋 目录

- [快速开始](#-快速开始)
- [安装](#-安装)
- [使用指南](#-使用指南)
- [命令参考](#-命令参考)
- [配置说明](#-配置说明)
- [架构设计](#-架构设计)
- [常见问题](#-常见问题)
- [开发指南](#-开发指南)

## 🚀 快速开始

### 前置要求

- Linux 操作系统 (x86_64)
- GCC 8.0+ (支持 C++17)
- CMake 3.14+
- NVIDIA GPU (可选，用于 GPU 任务)

### 编译安装

```bash
# 克隆仓库
git clone https://github.com/yourusername/myqueue.git
cd myqueue

# 完全静态编译（推荐）
cmake -S . -B build_full_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_FULLY_STATIC=ON \
    -DBUILD_TESTS=OFF
cmake --build build_full_static -j$(nproc)

# 可执行文件位于
./build_full_static/myqueue
```

详细编译选项请参考 [INSTALL.md](INSTALL.md)。

### 基本使用

```bash
# 1. 启动服务（后台运行）
myqueue server

# 2. 提交任务
myqueue sb job.sh --ncpu 4 --ngpu 1

# 3. 查看队列
myqueue sq

# 4. 查看资源状态
myqueue res

# 5. 停止服务
myqueue stop
```

## 📦 安装

### 方式一：直接使用编译好的二进制文件

```bash
# 复制到用户 bin 目录
cp build_full_static/myqueue ~/bin/

# 或复制到系统目录（需要 root）
sudo cp build_full_static/myqueue /usr/local/bin/
```

### 方式二：使用 CMake 安装

```bash
# 安装到默认位置 /usr/local/bin
sudo cmake --install build_full_static

# 安装到自定义位置
cmake --install build_full_static --prefix ~/.local
```

### 验证安装

```bash
myqueue --version
# 输出: myqueue version 1.0.0
#       Author: rcz
```

## 📖 使用指南

### 启动服务

```bash
# 基本启动
myqueue server

# 启动并启用日志
myqueue server --log ~/.myqueue/logs

# 启用任务日志输出到工作目录
myqueue server --joblog

# 自定义资源阈值
myqueue server --gpumemory 3000 --cpuusage 50

# 排除特定的 CPU 和 GPU（手动禁用）
myqueue server --excpus "0,1,2,3,4,32,33,34,35,36" --exgpus "0,4"

# 前台运行（调试用）
myqueue server --foreground

# 初始化队列后启动
myqueue server --init
```

### 提交任务

#### 单个任务

```bash
# 基本提交（默认 1 CPU + 1 GPU）
myqueue sb job.sh

# 指定资源数量
myqueue sb job.sh --ncpu 8 --ngpu 2

# 指定工作目录
myqueue sb job.sh -w /path/to/workdir

# 指定特定的 CPU 和 GPU
myqueue sb job.sh --cpus "0,1,2,3" --gpus "0,1"

# 自定义日志文件名
myqueue sb job.sh --logfile my_job.log
```

#### 批量提交

创建工作目录列表文件 `workdirs.txt`:
```
/home/user/calc1
/home/user/calc2
/home/user/calc3
# 注释行会被忽略
/home/user/calc4
```

提交批量任务:
```bash
myqueue sb job.sh -ws workdirs.txt --ncpu 4 --ngpu 1
```

### 查看队列

```bash
# 查看运行中和等待中的任务
myqueue sq

# 查看所有任务（包括已完成）
myqueue sq all

# 只显示汇总信息
myqueue sq -s
```

输出示例:
```
ID      STATUS      DURATION    CPUS                GPUS           WORKDIR
--------------------------------------------------------------------------------
1       RUNNING     00:15:32    0,1,2,3             0,1            /home/user/calc1
2       RUNNING     00:08:15    4,5,6,7             2,3            /home/user/calc2
3       PENDING     -           -                   -              /home/user/calc3

Total: 2 running, 1 pending
```

### 查看任务详情

```bash
# 查看任务详细信息
myqueue info 5

# 查看任务日志
myqueue log 5

# 查看最后 50 行日志
myqueue log 5 -n 50
```

### 删除任务

```bash
# 删除单个任务
myqueue del 5

# 删除多个任务
myqueue del 5 6 7

# 删除任务范围
myqueue del 1-10

# 删除所有任务
myqueue del all
```

### 查看资源状态

```bash
myqueue res
```

输出示例:
```
=== GPU Status ===
ID    STATUS      MEMORY              USAGE
------------------------------------------------------------
0     IDLE        512/11264 MB        4.5%
1     IDLE        256/11264 MB        2.3%
2     BUSY        8192/11264 MB       72.7%
3     BUSY        9500/11264 MB       84.3%

GPU Summary: 2 idle, 2 busy (threshold: 2000 MB)

=== CPU Status ===
Group 1 (CPU 0-31, GPU 0-3 affinity):
  Idle: 0,1,2,3,8,9,10,11,16,17,18,19,24,25,26,27
  Busy: 4,5,6,7,12,13,14,15,20,21,22,23,28,29,30,31
  Summary: 16 idle, 16 busy

Group 2 (CPU 32-63, GPU 4-7 affinity):
  Idle: 32,33,34,35,40,41,42,43,48,49,50,51,56,57,58,59
  Busy: 36,37,38,39,44,45,46,47,52,53,54,55,60,61,62,63
  Summary: 16 idle, 16 busy

CPU Total: 32 idle, 32 busy (threshold: 40%)
```

### 管理服务

```bash
# 停止服务
myqueue stop

# 初始化队列（清空所有任务）
myqueue init

# 查看版本
myqueue --version

# 查看帮助
myqueue --help
```

## 📚 命令参考

### 服务管理

| 命令 | 说明 |
|------|------|
| `myqueue server` | 启动后台守护进程 |
| `myqueue server --log <dir>` | 启动并启用日志 |
| `myqueue server --joblog` | 启用任务日志输出 |
| `myqueue server --init` | 初始化队列后启动 |
| `myqueue server --excpus "x,y,z"` | 排除特定 CPU 核心 |
| `myqueue server --exgpus "x,y,z"` | 排除特定 GPU 设备 |
| `myqueue server --foreground` | 前台运行（不守护进程化） |
| `myqueue stop` | 停止服务 |
| `myqueue init` | 初始化/重置队列数据 |

### 任务管理

| 命令 | 说明 |
|------|------|
| `myqueue sb <script>` | 提交任务 |
| `myqueue sb <script> --ncpu N` | 指定 CPU 核心数 |
| `myqueue sb <script> --ngpu N` | 指定 GPU 设备数 |
| `myqueue sb <script> --cpus "x,y,z"` | 指定特定 CPU 核心 |
| `myqueue sb <script> --gpus "x,y,z"` | 指定特定 GPU 设备 |
| `myqueue sb <script> -w <dir>` | 指定工作目录 |
| `myqueue sb <script> -ws <file>` | 批量提交（工作目录列表） |
| `myqueue sb <script> --logfile <name>` | 自定义日志文件名 |

### 队列查询

| 命令 | 说明 |
|------|------|
| `myqueue sq` | 查看运行中和等待中的任务 |
| `myqueue sq all` | 查看所有任务（包括已完成） |
| `myqueue sq -s` | 只显示汇总信息 |
| `myqueue info <id>` | 查看任务详细信息 |
| `myqueue log <id>` | 查看任务日志 |
| `myqueue log <id> -n <lines>` | 查看最后 N 行日志 |

### 任务删除

| 命令 | 说明 |
|------|------|
| `myqueue del <id>` | 删除指定任务 |
| `myqueue del <id1> <id2> ...` | 删除多个任务 |
| `myqueue del <start>-<end>` | 删除任务范围 |
| `myqueue del all` | 删除所有任务 |

### 资源监控

| 命令 | 说明 |
|------|------|
| `myqueue res` | 查看 CPU/GPU 资源状态 |

### 其他

| 命令 | 说明 |
|------|------|
| `myqueue --version` | 查看版本信息 |
| `myqueue --help` | 查看帮助信息 |

## ⚙️ 配置说明

### 服务器配置选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--log <path>` | 无 | 日志目录路径 |
| `--joblog` | 关闭 | 启用任务日志输出到工作目录 |
| `--gpumemory <MB>` | 2000 | GPU 忙碌阈值（显存使用量，MB） |
| `--cpuusage <percent>` | 40 | CPU 忙碌阈值（利用率百分比） |
| `--excpus "x,y,z"` | 无 | 排除特定 CPU 核心（逗号分隔） |
| `--exgpus "x,y,z"` | 无 | 排除特定 GPU 设备（逗号分隔） |
| `--foreground` | 关闭 | 前台运行（不守护进程化） |
| `--init` | 关闭 | 启动前初始化队列 |

### 数据存储位置

- **Socket 文件**: `/tmp/myqueue_<username>.sock`
- **数据目录**: `~/.myqueue/<hostname>/`
- **任务数据**: `~/.myqueue/<hostname>/tasks.json`
- **日志文件**: 由 `--log` 选项指定

### 环境变量

任务执行时会设置以下环境变量:

| 变量 | 说明 | 示例 |
|------|------|------|
| `CUDA_VISIBLE_DEVICES` | 可见的 GPU 设备 | `0,1` |
| `MYQUEUE_GPUS` | 分配的 GPU 设备 | `0,1` |
| `MYQUEUE_CPUS` | 分配的 CPU 核心 | `0,1,2,3` |

## 🏗️ 架构设计

### 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    CLI Client (myqueue)                  │
│  Commands: server, sb, sq, del, info, log, res, stop    │
└────────────────────┬────────────────────────────────────┘
                     │ Unix Domain Socket
                     │ (IPC Communication)
┌────────────────────▼────────────────────────────────────┐
│                  Server Daemon (myqueue server)          │
│  ┌─────────────────────────────────────────────────┐   │
│  │              IPC Server                          │   │
│  │  (Handle client requests via JSON protocol)     │   │
│  └──────────────────┬──────────────────────────────┘   │
│                     │                                    │
│  ┌──────────────────▼──────────────────────────────┐   │
│  │              Task Queue                          │   │
│  │  (Manage tasks: submit, query, delete)          │   │
│  │  Persistence: ~/.myqueue/<hostname>/tasks.json  │   │
│  └──────────────────┬──────────────────────────────┘   │
│                     │                                    │
│  ┌──────────────────▼──────────────────────────────┐   │
│  │              Scheduler                           │   │
│  │  - Scheduling loop (1s interval)                │   │
│  │  - Process monitoring (500ms interval)          │   │
│  │  - FIFO task scheduling                         │   │
│  └──────┬───────────────────────────────┬──────────┘   │
│         │                               │                │
│  ┌──────▼──────────┐           ┌───────▼──────────┐   │
│  │ Resource Monitor│           │    Executor       │   │
│  │ - GPU Monitor   │           │ - Fork processes  │   │
│  │ - CPU Monitor   │           │ - Setup env vars  │   │
│  │ - Affinity mgmt │           │ - Manage logs     │   │
│  └─────────────────┘           └───────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 核心组件

#### 1. IPC 通信层
- **协议**: JSON over Unix Domain Socket
- **消息类型**: SUBMIT, QUERY_QUEUE, DELETE_TASK, GET_TASK_INFO, GET_TASK_LOG, SHUTDOWN
- **特点**: 轻量级、低延迟、用户级隔离

#### 2. 任务队列 (TaskQueue)
- **数据结构**: `std::map<uint64_t, Task>`
- **持久化**: JSON 格式存储到 `~/.myqueue/<hostname>/tasks.json`
- **线程安全**: 使用 `std::mutex` 保护
- **任务状态**: PENDING → RUNNING → COMPLETED/FAILED/CANCELLED

#### 3. 调度器 (Scheduler)
- **调度策略**: FIFO（先进先出）
- **调度循环**: 1 秒间隔检查待调度任务
- **监控循环**: 500 毫秒间隔检查运行中任务状态
- **资源分配**: 先分配 GPU，再根据 GPU 确定 CPU 亲和性组

#### 4. 资源监控 (ResourceMonitor)
- **GPU 监控**: 通过 `nvidia-smi` 查询显存使用
- **CPU 监控**: 读取 `/proc/stat` 计算利用率
- **CPU 检查**: 连续 3 秒监控，确保 CPU 真正空闲
- **亲和性规则**:
  - GPU 0-3 → CPU 0-31 (NUMA Node 0)
  - GPU 4-7 → CPU 32-63 (NUMA Node 1)

#### 5. 执行器 (Executor)
- **进程管理**: `fork()` + `execl()` 执行任务脚本
- **进程组**: 使用 `setpgid(0, 0)` 创建新进程组
- **终止策略**: 先 SIGTERM (2秒)，再 SIGKILL (1秒)
- **日志管理**: 重定向 stdout/stderr 到日志文件

### 调度算法

```
1. 获取待调度任务列表（PENDING 状态，按提交时间排序）
2. 取第一个任务（FIFO）
3. 尝试分配资源:
   a. 分配 GPU（按 0,1,2,3,4,5,6,7 顺序）
   b. 根据 GPU 确定 CPU 亲和性组
   c. 随机选择 CPU（每个 CPU 需通过 3 秒连续监控）
4. 如果资源分配成功:
   a. 执行任务（fork + exec）
   b. 更新任务状态为 RUNNING
   c. 保存队列状态
5. 如果资源不足，等待下一个调度周期
```

### CPU-GPU 亲和性

```
NUMA Node 0                    NUMA Node 1
┌─────────────────┐           ┌─────────────────┐
│  CPU 0-31       │           │  CPU 32-63      │
│  ┌───┬───┬───┐  │           │  ┌───┬───┬───┐  │
│  │GPU│GPU│GPU│  │           │  │GPU│GPU│GPU│  │
│  │ 0 │ 1 │ 2 │  │           │  │ 4 │ 5 │ 6 │  │
│  └───┴───┴───┘  │           │  └───┴───┴───┘  │
│  ┌───┐          │           │  ┌───┐          │
│  │GPU│          │           │  │GPU│          │
│  │ 3 │          │           │  │ 7 │          │
│  └───┘          │           │  └───┘          │
└─────────────────┘           └─────────────────┘
```

## ❓ 常见问题

### Q: 如何在没有 GPU 的机器上使用？

A: MyQueue 可以在没有 GPU 的环境下运行，只调度 CPU 任务。提交任务时设置 `--ngpu 0`:
```bash
myqueue sb job.sh --ncpu 4 --ngpu 0
```

### Q: 任务被删除后进程还在运行？

A: MyQueue 使用进程组管理，删除任务时会终止整个进程树。如果发现进程残留，可能是:
1. 任务脚本中使用了 `nohup` 或 `disown`
2. 子进程忽略了 SIGTERM 信号

解决方法：确保脚本中的进程能正确响应信号。

### Q: 如何查看服务器日志？

A: 启动服务时指定日志目录:
```bash
myqueue server --log ~/.myqueue/logs
```
日志文件位于 `~/.myqueue/logs/server.log`。

### Q: 如何临时禁用某些 CPU 或 GPU？

A: 启动服务时使用 `--excpus` 和 `--exgpus` 选项：
```bash
# 排除 CPU 0-4 和 32-36，排除 GPU 0 和 4
myqueue server --excpus "0,1,2,3,4,32,33,34,35,36" --exgpus "0,4"
```
被排除的资源即使空闲也不会被分配给任务。这在以下场景很有用：
- 某些 GPU 有硬件问题需要维护
- 预留部分资源给其他用户或任务
- 测试特定资源配置

### Q: 多个用户可以同时使用吗？

A: 可以。每个用户有独立的:
- Socket 文件: `/tmp/myqueue_<username>.sock`
- 数据目录: `~/.myqueue/<hostname>/`

不同用户的 MyQueue 实例完全隔离。

### Q: 如何在集群上使用？

A: 在每个计算节点上:
1. 复制 `myqueue` 可执行文件到节点
2. 启动服务: `myqueue server`
3. 提交任务: `myqueue sb job.sh`

每个节点独立管理自己的任务队列。

### Q: 任务失败后如何重新提交？

A: 查看失败任务的详情:
```bash
myqueue info <id>
myqueue log <id>
```
修复问题后重新提交相同的脚本。

### Q: 如何限制单个用户的资源使用？

A: 当前版本不支持资源配额。可以通过以下方式限制:
1. 提交任务时手动控制 `--ncpu` 和 `--ngpu`
2. 修改服务器配置的资源阈值

### Q: 静态编译后的二进制文件很大？

A: 完全静态编译会包含所有依赖库，文件较大是正常的。如果需要减小体积:
1. 使用 `BUILD_STATIC=ON` 而不是 `BUILD_FULLY_STATIC=ON`
2. 使用 `strip` 命令去除调试符号: `strip myqueue`

## 🛠️ 开发指南

### 编译测试版本

```bash
cmake -S . -B build_test \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTS=ON
cmake --build build_test -j$(nproc)

# 运行测试
cd build_test && ctest --output-on-failure
```

### 项目结构

```
myqueue/
├── include/myqueue/       # 头文件
│   ├── config.h          # 配置管理
│   ├── task.h            # 任务数据结构
│   ├── task_queue.h      # 任务队列
│   ├── scheduler.h       # 调度器
│   ├── executor.h        # 执行器
│   ├── resource_monitor.h # 资源监控
│   ├── cpu_monitor.h     # CPU 监控
│   ├── gpu_monitor.h     # GPU 监控
│   ├── server.h          # 服务器
│   ├── ipc_server.h      # IPC 服务端
│   ├── ipc_client.h      # IPC 客户端
│   ├── protocol.h        # 通信协议
│   └── errors.h          # 错误定义
├── src/
│   ├── cli/              # 命令行入口
│   │   └── main.cpp
│   ├── core/             # 核心功能实现
│   │   ├── config.cpp
│   │   ├── task.cpp
│   │   ├── task_queue.cpp
│   │   ├── scheduler.cpp
│   │   ├── executor.cpp
│   │   ├── resource_monitor.cpp
│   │   ├── cpu_monitor.cpp
│   │   ├── gpu_monitor.cpp
│   │   └── server.cpp
│   └── ipc/              # IPC 实现
│       ├── ipc_server.cpp
│       ├── ipc_client.cpp
│       └── protocol.cpp
├── tests/                # 单元测试
├── CMakeLists.txt        # 构建配置
├── README.md             # 本文件
├── INSTALL.md            # 安装指南
└── CHANGELOG.md          # 更新日志
```

### 代码规范

- **C++ 标准**: C++17
- **命名规范**: 
  - 类名: `PascalCase`
  - 函数名: `camelCase`
  - 变量名: `snake_case`
  - 成员变量: `snake_case_` (后缀下划线)
- **注释**: Doxygen 风格
- **格式化**: 使用 clang-format

### 贡献指南

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支: `git checkout -b feature/amazing-feature`
3. 提交更改: `git commit -m 'Add amazing feature'`
4. 推送分支: `git push origin feature/amazing-feature`
5. 提交 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 👤 作者

**rcz**

## 🙏 致谢

- [nlohmann/json](https://github.com/nlohmann/json) - JSON 解析库
- [Google Test](https://github.com/google/googletest) - 单元测试框架
- [RapidCheck](https://github.com/emil-e/rapidcheck) - 属性测试框架

## 📮 联系方式

如有问题或建议，请提交 [Issue](https://github.com/yourusername/myqueue/issues)。

---

**Star ⭐ 本项目如果对你有帮助！**
