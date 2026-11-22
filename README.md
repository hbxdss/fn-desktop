# 飞牛终端（fn-desktop-plus）

飞牛 NAS 的一体化桌面终端：**Windows 桌面客户端（C++ / Qt 6）** + **NAS 服务端（Node.js / FPK）** + **内置 Web 管理后台（原生 HTML/CSS/JS）**。

| 组成 | 技术 | 位置 |
| --- | --- | --- |
| Windows 客户端 | C++17 / Qt 6.7.2 / Qt WebEngine | `client-cpp/` |
| 服务端 | Node.js ≥ 22.7，仅 TLS 1.3，HTTPS 59120 | `server/fpk/` |
| Web 管理后台 | 原生 HTML/CSS/JS，零外链 | `server/fpk/app/public/` |

## 三分钟上手

### 1. 构建客户端

前置：Qt 6.7.2（msvc2019_64）、Visual Studio 2022 BuildTools、CMake ≥ 3.21、Ninja。

```powershell
cd client-cpp
powershell -ExecutionPolicy Bypass -File build.ps1 -Deploy
```

产物：`client-cpp\build\bin\FnDesktopPlus.exe`（本包已附带可直接运行的已编译版本，含 Qt 运行库）。

`build.ps1` 里 `$QtDir`、`$VcVars` 为绝对路径，若你的安装位置不同，请修改脚本顶部三个变量。

### 2. 运行服务端（本地调试）

```powershell
cd server\fpk\app
npm install
node gen_cert.js
node server.js
```

默认监听 `https://0.0.0.0:59120`，浏览器打开 `https://127.0.0.1:59120/`（自签证书，需手动信任一次）。

### 3. 部署到飞牛 NAS

把 `server/fpk` 打包成 `.fpk`，在飞牛应用中心手动安装；或在 NAS 上直接运行：

```bash
cd /vol1/@appcenter/fnos-terminal/app
/vol1/@appcenter/fnos-terminal/runtime/node server.js
```

首次访问会进入「初始化」页面，创建超级管理员账号。

### 4. 连接客户端

启动 `FnDesktopPlus.exe`，填写：

- 服务器地址：`192.168.1.100`（示例，替换为你的 NAS 地址；也可直接填 `host:端口`）
- API 端口：`59120`
- 用户名 / 密码：初始化时创建的账号

客户端默认内嵌飞牛网页管理后台（`http://<NAS>:5666/`，开启 HTTPS 后为 5667），同时提供仪表盘、文件、容器、用户、日志、设置等原生页面。

## 安全说明

- 客户端**默认不保存密码、不保存登录令牌**，只持久化服务器地址、用户名与外观偏好；若在登录时勾选「保存密码」（默认关闭），密码会以系统加密（TPM 2.0 优先，DPAPI 回退，均绑定当前 Windows 用户）写入本机配置，换机/换用户无法解密。
- 客户端配置文件位于 `%APPDATA%\FNOS\FnDesktopPlus\config.json`；首次连接时记录服务端证书指纹（TOFU），证书变化时会弹窗提示，防止中间人替换证书。
- 服务端密码使用 Argon2id（N=2^15, t=3, p=4, salt 32B, 输出 64B）哈希；令牌仅存 SHA-256。
- 服务端**只允许 TLS 1.3**，证书为运行时自签（SAN 覆盖主机名与本机全部 IPv4）。
- 仓库内**不包含任何真实账号、密码、数据库或证书**。

## 详细文档

完整架构、API 清单、权限位、数据库结构、主题系统、FPK 打包与调试技巧见：

- [`docs/开发文档.md`](docs/开发文档.md)

## 版本

- 客户端：1.2.0
- 服务端：1.0.0
