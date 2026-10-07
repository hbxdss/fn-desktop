# 飞牛终端

飞牛 NAS 的 Windows 桌面客户端，基于 PySide6 + QtWebEngine 构建。

## 功能

- **FN Connect 智能搜索** — 输入 FN ID 后并行请求 `5ddd.com` 和 `fnos.net`，两个都可达时让用户选择，仅一个可达时直接连接
- **地址自适应** — 支持 FN ID（`mynas`）、完整域名（`mynas.5ddd.com`）、局域网 IP（`192.168.1.100:5666`）
- **凭据加密存储** — 三种模式自动降级
- **证书指纹锁定** — 首次连接显示 SHA-256 指纹，用户确认后记住；指纹变化时弹警告
- **账号密码自动填充** — 页面加载后通过 Qt 事件模拟键盘输入，不经过系统键盘
- **深色 UI** — 无边框窗口、自绘标题栏、飞牛绿主题
- **单窗口架构** — 网页中的新窗口链接原地加载

## 界面展示

![](    https://gitee.com/hbxdss/fn-desktop/raw/master/Image/1.png)

## 系统要求

- Windows 10 / 11
- TPM 2.0 模块（可选，用于硬件加密）

## 运行

### Python

下载仓库后执行

```
python main.py
```

### 免安装版&安装版

到Release里下载

## 使用

1. 启动程序
2. 输入 FN ID 或 NAS 地址
3. 可选：填写用户名密码并勾选「记住密码」
4. 点击「连接」

首次连接自签名证书的 NAS 时会弹出证书详情对话框，核对后点「仍然继续」。

如果飞牛账号开启了二次验证（2FA），程序会自动填入账号密码，验证码需手动输入。

## 安全说明

### 凭据存储位置

```
%USERPROFILE%\.fnos-browser\data.json
```

文件结构：

```
{
  "url": "飞牛IP或者fn id",
  "render_mode": "auto",
  "trusted_certs": { },
  "secure_blob": {
    "mode": "tpm",
    "data": "base64 加密数据"
  }
}
```

只有 `secure_blob` 是加密的，其他字段为明文。

### 三种加密模式

| 模式           | 触发条件         | 用户体验    | 安全性       |
| ------------ | ------------ | ------- | --------- |
| TPM 2.0 硬件加密 | 主板有 TPM 芯片   | 无感，自动解密 | 最高，密钥不出芯片 |
| 机器指纹加密       | 无 TPM 且账户无密码 | 无感      | 中，密钥与硬件绑定 |

所有加密数据均与当前机器绑定，拷到其他电脑无法解密。

### 证书指纹锁定

不盲目信任所有证书。首次连接显示证书 SHA-256 指纹、颁发者、有效期，用户确认后写入 `data.json`。后续连接自动比对：

- 指纹一致 → 直接通过
- 指纹变化 → 弹出红色警告

### 密码迁移与备份

菜单「🔐 密码迁移」可在不同加密方式之间切换。

菜单「📤 导出凭据」可将凭据导出为加密的 `.fnosbak` 文件，在另一台电脑上通过「📥 导入凭据」恢复。

## 项目结构

```目录结构
飞牛win/
├── main.py                  # 程序入口
├── config.py                # 全局 QSS 样式
├── data_store.py            # 统一数据管理
├── secure_store.py          # 凭据加密
├── trust_store.py           # 证书指纹存储
├── cert_dialog.py           # 证书信任对话框
├── password_dialog.py       # 密码输入对话框
├── migrate_dialog.py        # 密码迁移、导出、导入
├── search_dialog.py         # FN Connect 多结果选择
├── settings_dialog.py       # 渲染模式设置
├── fn_connect.py            # FN Connect 双域名搜索
├── custom_page.py           # 自定义 WebEnginePage
├── title_bar.py             # 自绘标题栏
├── browser.py               # 主窗口逻辑
├── keyboard_sim.py          # Qt 事件模拟键盘
├── inject.js                # 页面注入脚本
├── installer.iss            # Inno Setup 打包脚本
└── pages/
    ├── __init__.py
    ├── welcome.py           # 欢迎页
    ├── loading.py           # 加载中
    └── error.py             # 错误页
```

## 声明

本项目是飞牛 NAS 的第三方桌面客户端，与飞牛官方无关。用户需自行评估使用风险。

## 反馈

加[QQ群](https://qun.qq.com/universal-share/share?ac=1&authKey=v6r9sw4x0LymsY6HOAUiSUIlh2ff%2FhaPxJWM%2FRUpZloHx80UBFIb%2Folb0s9C3KDO&busi_data=eyJncm91cENvZGUiOiI2MjMyMzA3NDQiLCJ0b2tlbiI6ImNlNkVlM2o3R29mZlFsS2hlNjVCanM2M0VJQjV1eDE2T1hxejlra2hSZzZBR2RqOWlGbWFmZUlibFhvQVQ5Mm8iLCJ1aW4iOiIzODUxNTgyODIxIn0%3D&data=ZRubZigGM45bdSiL-eTICRrt-kMPwRZMwtWfp17GD-RYCLKYBqIhRWS3tbrhj09qEjmpP3A1pJUCkW1RKAH03Q&svctype=4&tempid=h5_group_info)反馈

![](https://gitee.com/hbxdss/fn-desktop/raw/master/Image/qrcode_1784783442708.jpg)

所有内容由 AI 生成
