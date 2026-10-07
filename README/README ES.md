# FN Terminal

Cliente de escritorio para Windows de Feiniu NAS, construido sobre PySide6 + QtWebEngine.

## Características

- **Búsqueda inteligente FN Connect** — Introduce el FN ID para realizar peticiones paralelas a `5ddd.com` y `fnos.net`. Si ambos son accesibles, permite elegir al usuario; si solo uno lo es, se conecta directamente.
- **Adaptación de direcciones** — Compatible con FN ID (`mynas`), dominio completo (`mynas.5ddd.com`) e IP de red local (`192.168.1.100:5666`).
- **Almacenamiento cifrado de credenciales** — Tres modos con degradación automática.
- **Bloqueo por huella digital de certificado** — Muestra la huella SHA‑256 en la primera conexión y la guarda tras la confirmación del usuario; muestra una advertencia si la huella cambia.
- **Autocompletado de cuenta y contraseña** — Después de cargar la página, simula la entrada de teclado mediante eventos Qt sin usar el teclado del sistema.
- **Interfaz oscura** — Ventana sin bordes, barra de título personalizada, tema verde Feiniu.
- **Arquitectura de ventana única** — Los enlaces de nuevas ventanas de páginas web se cargan en la misma ventana.

## Vista de interfaz

![](https://github.com/hbxdss/fn-desktop/blob/main/Image/1.png)

## Requisitos del sistema

- Windows 10 / 11
- Módulo TPM 2.0 (opcional, para cifrado por hardware)

## Ejecución

### Python

Descarga el repositorio y ejecuta:

```
python main.py
```

### Versión portátil e instalador

Descarga desde la página de Release.

## Uso

1. Inicia el programa.
2. Introduce el FN ID o la dirección del NAS.
3. Opcional: escribe nombre de usuario y contraseña y marca «Recordar contraseña».
4. Haz clic en «Conectar».

Al conectarte por primera vez a un NAS con certificado autofirmado, aparecerá un cuadro de diálogo con los detalles del certificado. Revísalos y pulsa «Continuar de todos modos».

Si la cuenta Feiniu tiene activada la autenticación de dos factores (2FA), el programa completará automáticamente la cuenta y la contraseña; el código de verificación debe introducirse manualmente.

## Notas de seguridad

### Ubicación de almacenamiento de credenciales

```
%USERPROFILE%\.fnos-browser\data.json
```

Estructura del archivo:

```json
{
  "url": "IP de Feiniu o FN ID",
  "render_mode": "auto",
  "trusted_certs": { },
  "secure_blob": {
    "mode": "tpm",
    "data": "datos cifrados en base64"
  }
}
```

Solo `secure_blob` está cifrado; los demás campos están en texto plano.

### Tres modos de cifrado

| Modo                          | Condición de activación                 | Experiencia de usuario              | Seguridad                            |
| ----------------------------- | --------------------------------------- | ----------------------------------- | ------------------------------------ |
| Cifrado hardware TPM 2.0      | La placa base dispone de chip TPM       | Transparente, descifrado automático | Máxima, las claves no salen del chip |
| Cifrado por huella de máquina | Sin TPM y la cuenta no tiene contraseña | Transparente                        | Media, claves vinculadas al hardware |

Todos los datos cifrados están vinculados al equipo actual y no se pueden descifrar al copiarlos a otro ordenador.

### Bloqueo por huella digital de certificado

El programa no confía ciegamente en todos los certificados. En la primera conexión muestra la huella SHA‑256 del certificado, el emisor y su periodo de validez. Tras la confirmación del usuario, se guarda en `data.json`. En conexiones posteriores se compara automáticamente:

- Huella coincidente → acceso directo
- Huella modificada → aparece una advertencia en rojo

### Migración y copia de seguridad de contraseñas

Usa el menú «🔐 Migrar contraseñas» para alternar entre métodos de cifrado.
Usa el menú «📤 Exportar credenciales» para exportar credenciales a un archivo cifrado `.fnosbak` y restáuralas en otro equipo mediante «📥 Importar credenciales».

## Estructura del proyecto

```
飞牛win/
├── main.py                  # Punto de entrada del programa
├── config.py                # Estilos QSS globales
├── data_store.py            # Gestión unificada de datos
├── secure_store.py          # Cifrado de credenciales
├── trust_store.py           # Almacenamiento de huellas de certificados
├── cert_dialog.py           # Diálogo de confianza de certificados
├── password_dialog.py       # Diálogo de introducción de contraseña
├── migrate_dialog.py        # Migración, exportación e importación de contraseñas
├── search_dialog.py         # Selección de múltiples resultados FN Connect
├── settings_dialog.py       # Ajustes del modo de renderizado
├── fn_connect.py            # Búsqueda en dos dominios FN Connect
├── custom_page.py           # WebEnginePage personalizado
├── title_bar.py             # Barra de título dibujada manualmente
├── browser.py               # Lógica de la ventana principal
├── keyboard_sim.py          # Simulación de teclado por eventos Qt
├── inject.js                # Script de inyección para páginas
├── installer.iss            # Script de empaquetado Inno Setup
└── pages/
    ├── __init__.py
    ├── welcome.py           # Página de bienvenida
    ├── loading.py           # Cargando
    └── error.py             # Página de error
```

## Descargo de responsabilidad

Este proyecto es un cliente de escritorio de terceros para Feiniu NAS y no tiene relación oficial con Feiniu. El usuario debe evaluar por su cuenta los riesgos de uso.

## Retroalimentación

Únete al [grupo QQ](https://qun.qq.com/universal-share/share?ac=1&authKey=v6r9sw4x0LymsY6HOAUiSUIlh2ff%2FhaPxJWM%2FRUpZloHx80UBFIb%2Folb0s9C3KDO&busi_data=eyJncm91cENvZGUiOiI2MjMyMzA3NDQiLCJ0b2tlbiI6ImNlNkVlM2o3R29mZlFsS2hlNjVCanM2M0VJQjV1eDE2T1hxejlra2hSZzZBR2RqOWlGbWFmZUlibFhvQVQ5Mm8iLCJ1aW4iOiIzODUxNTgyODIxIn0%3D&data=ZRubZigGM45bdSiL-eTICRrt-kMPwRZMwtWfp17GD-RYCLKYBqIhRWS3tbrhj09qEjmpP3A1pJUCkW1RKAH03Q&svctype=4&tempid=h5_group_info) para enviar comentarios.

![](https://github.com/hbxdss/fn-desktop/blob/main/Image/qrcode_1784783442708.jpg)

Todo el contenido generado por IA
