# 飛牛終端

飛牛 NAS 的 Windows 桌面用戶端，基於 PySide6 + QtWebEngine 建構。

## 功能

- **FN Connect 智慧搜尋** — 輸入 FN ID 後並行請求 `5ddd.com` 和 `fnos.net`，兩者皆可連線時讓使用者選擇，僅其中一者可連線則直接連接
- **位址自適應** — 支援 FN ID（`mynas`）、完整網域名稱（`mynas.5ddd.com`）、區域網路 IP（`192.168.1.100:5666`）
- **憑證加密儲存** — 三種模式自動降級
- **憑證指紋鎖定** — 首次連線顯示 SHA‑256 指紋，使用者確認後記錄；指紋發生變化時彈出警告
- **帳號密碼自動填入** — 頁面載入完成後透過 Qt 事件模擬鍵盤輸入，不經過系統鍵盤
- **深色 UI** — 無邊框視窗、自繪標題列、飛牛綠主題
- **單視窗架構** — 網頁內的新視窗連結於原地載入

## 介面展示

![](https://github.com/hbxdss/fn-desktop/blob/main/Image/1.png)

## 系統需求

- Windows 10 / 11
- TPM 2.0 模組（選用，用於硬體加密）

## 執行

### Python

下載儲存庫之後執行

```
python main.py
```

### 免安裝版&安裝版

前往 Release 頁面下載

## 使用

1. 啟動程式
2. 輸入 FN ID 或是 NAS 位址
3. 選填：填寫使用者名稱與密碼，並勾選「記住密碼」
4. 點擊「連線」

首次連接使用自簽憑證的 NAS 時，會彈出憑證詳細對話視窗，確認內容後點選「仍然繼續」。
如果飛牛帳號已開啟雙重驗證（2FA），程式會自動填入帳號密碼，驗證碼需要手動輸入。

## 安全說明

### 憑證儲存位置

```
%USERPROFILE%\.fnos-browser\data.json
```

檔案結構：

```json
{
  "url": "飛牛IP或者fn id",
  "render_mode": "auto",
  "trusted_certs": { },
  "secure_blob": {
    "mode": "tpm",
    "data": "base64 加密資料"
  }
}
```

只有 `secure_blob` 欄位為加密狀態，其餘欄位皆為明文。

### 三種加密模式

| 模式           | 觸發條件           | 使用者體驗   | 安全性         |
| ------------ | -------------- | ------- | ----------- |
| TPM 2.0 硬體加密 | 主機板具備 TPM 晶片   | 無感，自動解密 | 最高，金鑰不會離開晶片 |
| 機器指紋加密       | 無 TPM 且帳戶未設定密碼 | 無感      | 中等，金鑰與硬體綁定  |

所有加密資料皆與當前電腦綁定，複製到其他電腦將無法解密。

### 憑證指紋鎖定

不會盲目信任所有憑證。首次連線會顯示憑證 SHA‑256 指紋、簽發者與有效期限，經使用者確認後寫入 `data.json`。後續連線會自動比對：

- 指紋一致 → 直接通過
- 指紋發生變化 → 彈出紅色警告

### 密碼移轉與備份

選單「🔐 密碼移轉」可在不同加密方式之間切換。
選單「📤 匯出憑證」可以把憑證匯出為加密的 `.fnosbak` 檔案，在另一台電腦透過「📥 匯入憑證」功能還原。

## 專案結構

```
飛牛win/
├── main.py                  # 程式進入點
├── config.py                # 全域 QSS 樣式
├── data_store.py            # 統一資料管理
├── secure_store.py          # 憑證加密
├── trust_store.py           # 憑證指紋儲存
├── cert_dialog.py           # 憑證信任對話視窗
├── password_dialog.py       # 密碼輸入對話視窗
├── migrate_dialog.py        # 密碼移轉、匯出、匯入
├── search_dialog.py         # FN Connect 多筆結果選擇
├── settings_dialog.py       # 渲染模式設定
├── fn_connect.py            # FN Connect 雙網域搜尋
├── custom_page.py           # 自訂 WebEnginePage
├── title_bar.py             # 自繪標題列
├── browser.py               # 主視窗邏輯
├── keyboard_sim.py          # Qt 事件模擬鍵盤
├── inject.js                # 頁面注入指令碼
├── installer.iss            # Inno Setup 封裝指令稿
└── pages/
    ├── __init__.py
    ├── welcome.py           # 歡迎頁
    ├── loading.py           # 載入中
    └── error.py             # 錯誤頁
```

## 聲明

本專案為飛牛 NAS 的第三方桌面用戶端，和飛牛官方沒有關聯。使用者需自行評估使用風險。

## 回饋

加[QQ群](https://qun.qq.com/universal-share/share?ac=1&authKey=v6r9sw4x0LymsY6HOAUiSUIlh2ff%2FhaPxJWM%2FRUpZloHx80UBFIb%2Folb0s9C3KDO&busi_data=eyJncm91cENvZGUiOiI2MjMyMzA3NDQiLCJ0b2tlbiI6ImNlNkVlM2o3R29mZlFsS2hlNjVCanM2M0VJQjV1eDE2T1hxejlra2hSZzZBR2RqOWlGbWFmZUlibFhvQVQ5Mm8iLCJ1aW4iOiIzODUxNTgyODIxIn0%3D&data=ZRubZigGM45bdSiL-eTICRrt-kMPwRZMwtWfp17GD-RYCLKYBqIhRWS3tbrhj09qEjmpP3A1pJUCkW1RKAH03Q&svctype=4&tempid=h5_group_info)回饋

![](https://github.com/hbxdss/fn-desktop/blob/main/Image/qrcode_1784783442708.jpg)

所有內容由 AI 生成
