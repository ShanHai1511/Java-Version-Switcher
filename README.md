# Java Version Switcher (JVS) v2.0

> 极致轻量 Windows Java 版本切换工具，纯 C + Win32 API 编写，零运行时依赖。

![C](https://img.shields.io/badge/Language-C-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Size](https://img.shields.io/badge/Size-2.5%20MB%20(UPX%20after)-brightgreen)

## 痛点

- 手动改 `JAVA_HOME` 繁琐易错，Path 残留旧 JDK 导致混乱
- 现有 Java 版本 GUI 工具需要**先装 JDK 才能切换 JDK**（鸡生蛋悖论）
- JavaFX/Swing 工具启动慢（~5 秒）、体积臃肿（~60MB）

## JVS v2 解决方案

| 特性 | 传统 JavaFX 工具 | JVS v2.0 |
|------|-----------------|----------|
| 运行时依赖 | 必须预装 JDK 17 | **零依赖** |
| 启动时间 | ~5 秒 | **< 30ms** |
| 单文件大小 | ~60MB | **2.5MB（UPX 压缩后）** |
| 权限模型 | 全程管理员 | **仅切换时提权** |
| 配置格式 | JSON（需转义） | **INI（可直接编辑）** |
| 编程语言 | Java + JavaFX | **C + Win32 API** |

## 快速开始

### 下载

从 [Releases](https://github.com/ShanHai1511/Java-Version-Switcher/releases) 下载 `jvs_v2.exe`，双击即可运行。

### 手动构建

**前置条件：** Visual Studio 2022 Build Tools（MSVC cl）

```powershell
# 克隆仓库
git clone https://github.com/ShanHai1511/Java-Version-Switcher.git
cd Java-Version-Switcher

# 构建（Debug）
.\build_v2.ps1 -Configuration Debug

# 构建（Release，带 UPX 压缩）
.\build_v2.ps1 -Configuration Release
upx --best build\jvs_v2.exe
```

输出：`build\jvs_v2.exe`

## 功能

### JDK 扫描
- 自动扫描 `C:\Program Files\Java\`、注册表 `HKLM\SOFTWARE\JavaSoft\JDK`
- 智能深度控制：`C:\Program Files` 系列宽泛目录仅扫描 2 层，具体 JDK 目录扫描 3 层，**扫描 < 20ms**
- 执行 `java -version` 解析精确版本号和厂商（Adoptium / Oracle / Amazon / Microsoft 等）
- Minecraft 版本自动标记（Java 8 `[1.12-]` / 17 `[1.18-1.20]` / 21 `[1.21+]`）

### 版本切换
- 一键切换 `JAVA_HOME` + 智能清洗 `Path`（过滤旧 JDK 项）
- 切换前全量备份注册表到 `%APPDATA%\JVS\backup\`
- 异常自动回滚（写入失败、Path 过短等）
- `SendMessageTimeout` 广播环境变更，**无需重启即可生效**

### JDK 下载
- 内置华为云镜像源，支持断点续传
- 下载后自动解压到 `%USERPROFILE%\.jvs\jdk\`
- 支持的版本：8 / 11 / 17 / 21 / 23

### 安全设计
- **最小权限原则**：浏览、扫描、下载以普通用户运行
- **仅切换时提权**：通过 `ShellExecute("runas")` 启动子进程
- **自动回滚**：任何写入失败自动恢复备份

## 项目结构

```
├── main.c               # 入口（GUI / CLI --scan / --switch）
├── core.c / core.h      # JDKInfo 结构、扫描器、切换逻辑、注册表操作
├── config.c / config.h  # INI 配置读写
├── gui.c / gui.h        # Win32 窗口、ListView、按钮、状态栏
├── util.c / util.h      # 字符串工具、格式化、路径处理
├── build_v2.ps1         # MSVC 构建脚本
└── build\_build.cmd     # MSVC 自动生成的编译命令
```

## 技术栈

- **语言**：C11
- **编译器**：MSVC（Visual Studio 2022 Build Tools）
- **GUI**：Win32 API（`user32.dll` / `gdi32.dll`），无第三方 GUI 框架
- **注册表**：`advapi32.dll`（原生 Win32 API）
- **配置**：INI 格式（自定义解析器，零依赖）
- **打包**：MSVC 原生编译 + UPX 压缩

## CLI 用法

```powershell
# 扫描所有 JDK，JSON 输出
.\jvs_v2.exe --scan

# 切换 JDK（需要管理员权限）
.\jvs_v2.exe --switch "D:\install\java\java17\jdk-17.0.12+7"

# 查看版本
.\jvs_v2.exe --version
```

## License

MIT
