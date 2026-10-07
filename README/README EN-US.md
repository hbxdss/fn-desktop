# FN Desktop

A Windows desktop client for Feiniu NAS, built with PySide6 + QtWebEngine.

## Features

- **FN Connect Smart Search** — Enter an FN ID to request both `5ddd.com` and `fnos.net` in parallel. If both are reachable, let the user choose; if only one is reachable, connect directly.
- **Address Adaptation** — Supports FN ID (`mynas`), full domain name (`mynas.5ddd.com`), and LAN IP (`192.168.1.100:5666`).
- **Encrypted Credential Storage** — Three modes with automatic fallback.
- **Certificate Fingerprint Lock** — On first connection, show the SHA‑256 fingerprint and remember it after user confirmation; warn if the fingerprint changes.
- **Automatic Account and Password Fill** — After the page loads, simulate keyboard input through Qt events without using the system keyboard.
- **Dark UI** — Borderless window, custom-drawn title bar, Feiniu green theme.
- **Single-Window Architecture** — New window links from web pages load in place.

## Interface Preview

![Program interface](https://github.com/hbxdss/fn-desktop/raw/main/Image/1.png)

## System Requirements

- Windows 10 / 11
- TPM 2.0 module (optional, for hardware encryption)

## Run

### Python

Download the repository and run:

```
python main.py
```

### Portable & Installer

Download from the Release page.

## Usage

1. Start the program.
2. Enter the FN ID or NAS address.
3. Optional: enter username and password, then check “Remember password”.
4. Click “Connect”.

When connecting for the first time to a NAS using a self-signed certificate, a certificate details dialog will appear. After checking the details, click “Continue anyway”.

If the Feiniu account has two-factor authentication (2FA) enabled, the program will fill in the account and password automatically; the verification code must be entered manually.

## Security Notes

### Credential Storage Location

```
%USERPROFILE%\.fnos-browser\data.json
```

File structure:

```json
{
  "url": "Feiniu IP or FN ID",
  "render_mode": "auto",
  "trusted_certs": { },
  "secure_blob": {
    "mode": "tpm",
    "data": "base64 encrypted data"
  }
}
```

Only `secure_blob` is encrypted; other fields are in plaintext.

### Three Encryption Modes

| Mode                           | Trigger Condition                  | User Experience                   | Security                           |
| ------------------------------ | ---------------------------------- | --------------------------------- | ---------------------------------- |
| TPM 2.0 Hardware Encryption    | Motherboard has a TPM chip         | Transparent, automatic decryption | Highest, keys never leave the chip |
| Machine Fingerprint Encryption | No TPM and account has no password | Transparent                       | Medium, keys are bound to hardware |

All encrypted data is bound to the current machine and cannot be decrypted when copied to another computer.

### Certificate Fingerprint Lock

The program does not blindly trust all certificates. On the first connection, the certificate SHA‑256 fingerprint, issuer, and validity period will be displayed. After user confirmation, they are written to `data.json`. Subsequent connections will automatically compare:

- Fingerprint matches → pass directly
- Fingerprint changes → red warning popup

### Password Migration and Backup

Use the “🔐 Password Migration” menu to switch between encryption methods.
Use the “📤 Export Credentials” menu to export credentials as an encrypted `.fnosbak` file, then restore them on another computer via “📥 Import Credentials”.

## Project Structure

```
飞牛win/
├── main.py                  # Program entry point
├── config.py                # Global QSS style
├── data_store.py            # Unified data management
├── secure_store.py          # Credential encryption
├── trust_store.py           # Certificate fingerprint storage
├── cert_dialog.py           # Certificate trust dialog
├── password_dialog.py       # Password input dialog
├── migrate_dialog.py        # Password migration, export, import
├── search_dialog.py         # FN Connect multi-result selection
├── settings_dialog.py       # Rendering mode settings
├── fn_connect.py            # FN Connect dual-domain search
├── custom_page.py           # Custom WebEnginePage
├── title_bar.py             # Custom-drawn title bar
├── browser.py               # Main window logic
├── keyboard_sim.py          # Qt event-based keyboard simulation
├── inject.js                # Page injection script
├── installer.iss            # Inno Setup packaging script
└── pages/
    ├── __init__.py
    ├── welcome.py           # Welcome page
    ├── loading.py           # Loading
    └── error.py             # Error page
```

## Disclaimer

This project is a third-party desktop client for Feiniu NAS and is not affiliated with Feiniu official. Users should evaluate the risks of use on their own.

## Feedback

Join the [QQ group](https://qun.qq.com/universal-share/share?ac=1&authKey=v6r9sw4x0LymsY6HOAUiSUIlh2ff%2FhaPxJWM%2FRUpZloHx80UBFIb%2Folb0s9C3KDO&busi_data=eyJncm91cENvZGUiOiI2MjMyMzA3NDQiLCJ0b2tlbiI6ImNlNkVlM2o3R29mZlFsS2hlNjVCanM2M0VJQjV1eDE2T1hxejlra2hSZzZBR2RqOWlGbWFmZUlibFhvQVQ5Mm8iLCJ1aW4iOiIzODUxNTgyODIxIn0%3D&data=ZRubZigGM45bdSiL-eTICRrt-kMPwRZMwtWfp17GD-RYCLKYBqIhRWS3tbrhj09qEjmpP3A1pJUCkW1RKAH03Q&svctype=4&tempid=h5_group_info) for feedback.

![QQ group QR code](https://github.com/hbxdss/fn-desktop/raw/main/Image/qrcode_1784783442708.jpg)



All content generated by AI
