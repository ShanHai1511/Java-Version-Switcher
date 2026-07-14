# Java Version Switcher (JVS) v2.0

> 极致轻量的 Windows Java 版本切换工具，纯 Go 编写，原生 Win32 界面。

[![Go Version](https://img.shields.io/badge/Go-1.22+-00ADD8)](https://golang.org)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)
[![Size](https://img.shields.io/badge/Size-2.5%20MB%20(UPX%20after)-brightgreen)]()

## 痛点

- 手动改 `JAVA_HOME` 繁琐易错
- 现有 Java 版 GUI 工具需要**先装 JDK 才能切换 JDK**（鸡生蛋悖论）
- 启动慢（~5 秒）、体积臃肿（~60MB）

## JVS 解决方案

| 特性 | 传统 JavaFX 工具 | JVS v2.0 |
|------|-----------------|----------|
| 运行时依赖 | 必须预装 JDK 17 | **零依赖** |
| 启动时间 | ~5 秒 | **< 30ms** |
| 单文件大小 | ~60MB | **2.5MB（UPX 压缩后）** |
| 权限模型 | 全程管理员 | **仅切换时提权** |
| 配置格式 | JSON（需转义） | **INI（可直接编辑）** |
| 编程语言 | Java + JavaFX | **Go + Win32 API** |

## 快速开始

### 下载

从 [Releases](https://github.com/ShanHai1511/Java-Version-Switcher/releases) 下载 `jvs.exe`，双击即可运行。

### 手动构建

```bash
# 前置条件：Go 1.22+
git clone https://github.com/ShanHai1511/Java-Version-Switcher.git
cd Java-Version-Switcher

# 开发版（带控制台窗口）
go build -o build\jvs.exe .

# 发布版（无控制台窗口 + 压缩）
go build -ldflags="-s -w -H=windowsgui" -o build\jvs.exe .
# 可选：upx --best build\jvs.exe
```

## 功能

### JDK 扫描
- 自动扫描 `C:\Program Files\Java\`、注册表 `HKLM\SOFTWARE\JavaSoft\JDK`
- 并行扫描多个路径，执行 `java -version` 解析精确版本号和厂商
- Minecraft 版本自动标记（Java 8 `[1.12-]`、17 `[1.18-1.20]`、21 `[1.21+]`）

### 版本切换
- 一键切换 `JAVA_HOME` + 智能清洗 `Path`（过滤旧 JDK 项）
- 切换前全量备份注册表到 `%APPDATA%\JVS\backup\`
- 异常自动回滚（写入失败、Path 过短等）
- `SendMessageTimeout` 广播，**无需重启即可生效**

### JDK 下载
- 内置华为云镜像源，支持断点续传
- 下载后自动解压到 `%USERPROFILE%\.jvs\jdk\`
- 支持的版本：8、11、17、21、22、23

### 安全设计
- **最小权限原则**：浏览、扫描、下载以普通用户运行
- **仅切换时提权**：通过 `ShellExecute("runas")` 启动子进程
- **自动回滚**：任何写入失败自动恢复备份

## 项目结构

```
├── main.go              # 入口（GUI 模式 / --switch 子进程模式）
├── core/
│   ├── config.go        # INI 配置读写
│   ├── registry_ops.go  # 注册表操作 + 环境变量广播
│   ├── scanner.go       # JDK 扫描器（目录 + 注册表）
│   ├── switcher.go      # 核心切换 + Path 清洗 + 自动回滚
│   └── downloader.go    # 镜像下载 + ZIP 解压
├── gui/
│   ├── window.go        # Win32 窗口 + 消息循环
│   └── controls.go      # ListView + 按钮 + 状态栏
├── resources/
│   └── jvs.manifest     # DPI 感知 + 通用控件 v6
└── build.ps1            # 构建脚本
```

## 技术栈

- **语言**：Go 1.22+
- **GUI**：Win32 API（`user32.dll`）— 零 GUI 框架
- **注册表**：`golang.org/x/sys/windows/registry`
- **配置文件**：INI 格式（自定义解析器，零依赖）
- **打包**：Go 原生编译 + UPX 压缩

## License

MIT
