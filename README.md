# 飞牛终端 · FnDesktopPlus

把飞牛 NAS 装进 Windows 桌面：SSH 终端、文件管理、Docker、用户与审计，一个客户端全搞定。

[![Release](https://img.shields.io/badge/release-v1.2.0-2f81f7)](https://github.com/hbxdss/fn-desktop/releases/tag/v1.2.0)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078d4)](#下载与安装)
[![Client](https://img.shields.io/badge/client-C%2B%2B17%20%2F%20Qt%206.7-41cd52)](#从源码构建)
[![Server](https://img.shields.io/badge/server-Node.js%20%E2%89%A5%2022.7-339933)](#从源码构建)
[![Gitee](https://img.shields.io/badge/Gitee-%E9%95%9C%E5%83%8F-c71d23)](https://gitee.com/hbxdss/fn-desktop)

## 项目组成

| 组成 | 技术 | 位置 |
| --- | --- | --- |
| Windows 客户端 | C++17 / Qt 6.7.2 / Qt WebEngine | [`client-cpp/`](client-cpp) |
| NAS 服务端 | Node.js ≥ 22.7，仅 TLS 1.3，HTTPS 59120 | [`server/fpk/`](server/fpk) |
| Web 管理后台 | 原生 HTML / CSS / JS，零外部依赖 | [`server/fpk/app/public/`](server/fpk/app/public) |

## 功能特性

- **SSH 终端**：多会话标签、SFTP 文件浏览与传输、密钥登录、一键提权，自动适配远端伪终端尺寸。
- **仪表盘**：CPU、内存、磁盘、网络与系统信息一屏总览。
- **文件管理**：目录浏览、上传下载、新建/重命名/删除、路径映射为「用户目录 › 子目录」的友好视图。
- **Docker 管理**：容器列表、状态过滤、启停与日志查看。
- **用户与审计**：多角色权限位、登录与操作审计日志。
- **内置管理后台**：客户端内嵌飞牛 Web 管理页面，也可直接用浏览器访问。
- **安全连接**：首次连接记录服务端证书指纹（TOFU），证书变化时弹窗确认，防中间人替换。

## 下载与安装

| 用途 | 文件 | 大小 | 下载 |
| --- | --- | --- | --- |
| Windows 客户端（推荐） | `FnDesktopPlus-v1.2.0-Setup.exe` | 74.8 MB | [GitHub](https://github.com/hbxdss/fn-desktop/releases/download/v1.2.0/FnDesktopPlus-v1.2.0-Setup.exe) · [Gitee](https://gitee.com/hbxdss/fn-desktop/releases/download/v1.2.0/FnDesktopPlus-v1.2.0-Setup.exe) |
| Windows 客户端（便携 zip） | `FnDesktopPlus-v1.2.0-win64.zip` | 102.1 MB | [GitHub](https://github.com/hbxdss/fn-desktop/releases/download/v1.2.0/FnDesktopPlus-v1.2.0-win64.zip) |
| Windows 客户端（便携 7z） | `FnDesktopPlus-v1.2.0-win64.7z` | 72.8 MB | [Gitee](https://gitee.com/hbxdss/fn-desktop/releases/download/v1.2.0/FnDesktopPlus-v1.2.0-win64.7z) |
| 飞牛 NAS 服务端 | `fnos-terminal-1.0.0-full.fpk` | 61.0 MB | [GitHub](https://github.com/hbxdss/fn-desktop/releases/download/v1.2.0/fnos-terminal-1.0.0-full.fpk) · [Gitee](https://gitee.com/hbxdss/fn-desktop/releases/download/v1.2.0/fnos-terminal-1.0.0-full.fpk) |

> Gitee 单个附件上限 100 MB，因此 102 MB 的 zip 在 Gitee 上提供的是内容相同的 7z 版本，用 7-Zip / Bandizip / WinRAR 解压即可。

### GitHub 访问慢或打不开？

GitHub 下载链接可通过加速站访问（把原始链接拼在加速站域名后面）：

- `https://ghproxy.net/https://github.com/hbxdss/fn-desktop/releases/download/v1.2.0/FnDesktopPlus-v1.2.0-Setup.exe`
- `https://ghfast.top/https://github.com/hbxdss/fn-desktop/releases/download/v1.2.0/FnDesktopPlus-v1.2.0-Setup.exe`

也可以直接使用上表中的 **Gitee** 链接，国内访问更稳定。

### 安装客户端

1. **安装版**：运行 `FnDesktopPlus-v1.2.0-Setup.exe`，按向导完成安装（需管理员权限，默认安装到 `Program Files\FnDesktopPlus`）。
2. **便携版**：解压 zip / 7z 到任意目录，双击 `FnDesktopPlus.exe`。

首次启动填写 NAS 地址、API 端口（默认 `59120`）和账号密码；账号在服务端首次初始化时创建。

### 部署服务端

1. 飞牛应用中心 → 手动安装 → 选择 `fnos-terminal-1.0.0-full.fpk`（含 Node.js 运行时）。
2. 浏览器打开 `https://<NAS 地址>:59120/`，首次访问进入初始化页面，创建超级管理员账号。
3. 自签证书首次访问会有安全提示，确认指纹后继续即可；客户端也会在首次连接时记录该指纹。

## 从源码构建

### 客户端

前置：Qt 6.7.2（msvc2019_64）、Visual Studio 2022 BuildTools、CMake ≥ 3.21、Ninja。

```powershell
cd client-cpp
powershell -ExecutionPolicy Bypass -File build.ps1 -Deploy
```

产物在 `client-cpp\build\bin\FnDesktopPlus.exe`，`-Deploy` 会一并复制 Qt 运行库。

`build.ps1` 顶部的 `$QtDir`、`$VcVars`、`$CMake`、`$Ninja` 是绝对路径，安装位置不同时按需修改。

### 服务端

```powershell
cd server\fpk\app
npm install
node gen_cert.js
node server.js
```

默认监听 `https://0.0.0.0:59120`，本机打开 `https://127.0.0.1:59120/`。

### Windows 安装包

安装 [Inno Setup 6](https://jrsoftware.org/isdl.php) 后执行：

```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" client-cpp\installer\FnDesktopPlus.iss
```

产物输出到 `dist\release\FnDesktopPlus-v1.2.0-Setup.exe`。

## 仓库结构

```text
client-cpp/            Windows 客户端（C++ / Qt 6）
  src/                 界面、SSH、SFTP、网络与配置
  resources/           图标与应用资源
  installer/           Inno Setup 安装包脚本
server/fpk/            飞牛 FPK 服务端
  app/                 Node.js 服务、Web 后台、SQLite 数据层
  cmd/                 FPK 安装/升级/卸载钩子
  config/ manifest      飞牛应用配置
docs/开发文档.md        架构、API、数据库、打包与调试说明
```

## 分支策略

| 分支 | 角色 | 说明 |
| --- | --- | --- |
| `new` | 默认主分支 | 重构后的新版本，所有新功能与日常迭代都在这里开展 |
| `legacy-old` | 归档分支 | 由原 `old` 分支重命名而来，对应历史旧版 Release 代码 |

- `legacy-old` 不再主动维护、不再同步新功能，只做存档保留。
- 一律不要把 `legacy-old` 合并进 `new`——该方向会用旧代码覆盖重构版本。
- 仅极个别紧急修复需要时，单独把对应提交 cherry-pick 到 `new`，禁止整分支合并。
- 不要创建 `base=new`、`compare=legacy-old` 的合并请求。

## 安全设计

- 客户端默认**不保存密码、不保存登录令牌**，仅持久化服务器地址、用户名与外观偏好。
- 勾选「保存密码」后，密码以系统加密存储（TPM 2.0 优先，DPAPI 回退，均绑定当前 Windows 用户）。
- 服务端密码使用 **Argon2id**（N=2^15, r=8, p=4, salt 32B, 输出 64B）哈希，令牌只存 SHA-256。
- 服务端**只允许 TLS 1.3**，证书运行时自签，SAN 覆盖主机名与本机全部 IPv4。
- 客户端首次连接记录证书指纹（TOFU），证书变化时弹窗确认。
- 仓库内**不包含任何真实账号、密码、数据库或证书**。

## 常见问题

**客户端提示证书不安全？** 服务端使用自签证书，首次连接需确认指纹；后续指纹变化会强制弹窗，请核对是否为你本人更换的证书。

**端口冲突？** 服务端默认 `59120`，Web 管理后台默认 `5666/5667`，可在服务端配置中调整。

**GitHub 打不开？** 使用上方加速站链接，或直接走 Gitee 发行版。

**安装版和便携版有什么区别？** 安装版写入开始菜单与卸载项，可创建桌面快捷方式；便携版解压即用，配置同样保存在 `%APPDATA%\FNOS\FnDesktopPlus`。

## 版本

- 客户端：**1.2.0**
- 服务端：**1.0.0**

完整架构、REST API、权限位、数据库结构、FPK 打包与调试技巧见 [docs/开发文档.md](docs/开发文档.md)。

## 友情链接

- GitHub：<https://github.com/hbxdss/fn-desktop>
- Gitee 镜像：<https://gitee.com/hbxdss/fn-desktop>
