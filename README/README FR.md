# Terminal FN

Client de bureau Windows pour Feiniu NAS, construit avec PySide6 + QtWebEngine.

## Fonctionnalités

- **Recherche intelligente FN Connect** — Saisissez un identifiant FN pour envoyer des requêtes parallèles vers `5ddd.com` et `fnos.net`. Si les deux sont accessibles, laissez l’utilisateur choisir ; si seul l’un est joignable, la connexion s’établit directement.
- **Adaptation d’adresse** — Prend en charge l’identifiant FN (`mynas`), le nom de domaine complet (`mynas.5ddd.com`) et l’adresse IP du réseau local (`192.168.1.100:5666`).
- **Stockage chiffré des identifiants** — Trois modes avec rétrogradation automatique.
- **Verrouillage par empreinte de certificat** — Affiche l’empreinte SHA‑256 lors de la première connexion et la mémorise après validation de l’utilisateur ; affiche un avertissement si l’empreinte change.
- **Remplissage automatique du compte et du mot de passe** — Une fois la page chargée, simule la saisie clavier via des événements Qt sans passer par le clavier système.
- **Interface sombre** — Fenêtre sans bordure, barre de titre personnalisée, thème vert Feiniu.
- **Architecture à fenêtre unique** — Les liens ouvrant une nouvelle fenêtre web se chargent sur place.

## Aperçu de l’interface

![](https://github.com/hbxdss/fn-desktop/blob/main/Image/qrcode_1784783442708.jpg)

## Configuration système requise

- Windows 10 / 11
- Module TPM 2.0 (facultatif, pour le chiffrement matériel)

## Exécution

### Python

Téléchargez le dépôt puis exécutez :

```
python main.py
```

### Version portable et installateur

Téléchargez‑les depuis la page Release.

## Utilisation

1. Lancez le programme.
2. Saisissez l’identifiant FN ou l’adresse du NAS.
3. Facultatif : renseignez le nom d’utilisateur et le mot de passe, puis cochez « Se souvenir du mot de passe ».
4. Cliquez sur « Connexion ».

Lors de la première connexion à un NAS avec certificat auto‑signé, une boîte de dialogue avec les détails du certificat s’affiche. Vérifiez les informations puis cliquez sur « Continuer malgré tout ».

Si l’authentification à deux facteurs (2FA) est activée sur le compte Feiniu, le programme remplit automatiquement le compte et le mot de passe ; le code de vérification doit être saisi manuellement.

## Notes de sécurité

### Emplacement de stockage des identifiants

```
%USERPROFILE%\.fnos-browser\data.json
```

Structure du fichier :

```json
{
  "url": "IP Feiniu ou identifiant FN",
  "render_mode": "auto",
  "trusted_certs": { },
  "secure_blob": {
    "mode": "tpm",
    "data": "données chiffrées en base64"
  }
}
```

Seul le champ `secure_blob` est chiffré ; les autres champs sont en texte brut.

### Trois modes de chiffrement

| Mode                              | Condition de déclenchement                      | Expérience utilisateur                 | Sécurité                                     |
| --------------------------------- | ----------------------------------------------- | -------------------------------------- | -------------------------------------------- |
| Chiffrement matériel TPM 2.0      | La carte mère possède une puce TPM              | Transparent, déchiffrement automatique | Maximale, les clés ne sortent pas de la puce |
| Chiffrement par empreinte machine | Pas de TPM et le compte n’a pas de mot de passe | Transparent                            | Moyenne, les clés sont liées au matériel     |

Toutes les données chiffrées sont liées à la machine actuelle et ne peuvent pas être déchiffrées si elles sont copiées sur un autre ordinateur.

### Verrouillage par empreinte de certificat

Le programme ne fait pas confiance aveuglément à tous les certificats. Lors de la première connexion, l’empreinte SHA‑256 du certificat, son émetteur et sa période de validité sont affichés. Après validation utilisateur, ces informations sont écrites dans `data.json`. Les connexions suivantes effectuent une comparaison automatique :
‑ Empreinte identique → accès autorisé directement
‑ Empreinte modifiée → affichage d’un avertissement rouge

### Migration et sauvegarde des mots de passe

Utilisez le menu « 🔐 Migration des mots de passe » pour basculer entre les différents modes de chiffrement.
Utilisez le menu « 📤 Exporter les identifiants » pour exporter les identifiants dans un fichier chiffré `.fnosbak`, puis restaurez‑les sur un autre ordinateur via « 📥 Importer les identifiants ».

## Structure du projet

```
飞牛win/
├── main.py                  # Point d’entrée du programme
├── config.py                # Style QSS global
├── data_store.py            # Gestion unifiée des données
├── secure_store.py          # Chiffrement des identifiants
├── trust_store.py           # Stockage des empreintes de certificats
├── cert_dialog.py           # Boîte de dialogue d’approbation de certificat
├── password_dialog.py       # Boîte de dialogue de saisie du mot de passe
├── migrate_dialog.py        # Migration, export et import des mots de passe
├── search_dialog.py         # Sélection des multiples résultats FN Connect
├── settings_dialog.py       # Paramètres du mode de rendu
├── fn_connect.py            # Recherche sur deux domaines FN Connect
├── custom_page.py           # WebEnginePage personnalisé
├── title_bar.py             # Barre de titre dessinée manuellement
├── browser.py               # Logique de la fenêtre principale
├── keyboard_sim.py          # Simulation clavier par événements Qt
├── inject.js                # Script injecté dans les pages web
├── installer.iss            # Script de compilation Inno Setup
└── pages/
    ├── __init__.py
    ├── welcome.py           # Page de bienvenue
    ├── loading.py           # Chargement
    └── error.py             # Page d’erreur
```

## Avertissement

Ce projet est un client de bureau tiers pour Feiniu NAS, il n’est pas affilié officiellement à Feiniu. L’utilisateur doit évaluer les risques liés à son utilisation.

## Retour d’informations

Rejoignez le [groupe QQ](https://qun.qq.com/universal-share/share?ac=1&authKey=v6r9sw4x0LymsY6HOAUiSUIlh2ff%2FhaPxJWM%2FRUpZloHx80UBFIb%2Folb0s9C3KDO&busi_data=eyJncm91cENvZGUiOiI2MjMyMzA3NDQiLCJ0b2tlbiI6ImNlNkVlM2o3R29mZlFsS2hlNjVCanM2M0VJQjV1eDE2T1hxejlra2hSZzZBR2RqOWlGbWFmZUlibFhvQVQ5Mm8iLCJ1aW4iOiIzODUxNTgyODIxIn0%3D&data=ZRubZigGM45bdSiL-eTICRrt-kMPwRZMwtWfp17GD-RYCLKYBqIhRWS3tbrhj09qEjmpP3A1pJUCkW1RKAH03Q&svctype=4&tempid=h5_group_info) pour envoyer vos retours.

![Code QR du groupe QQ](https://github.com/hbxdss/fn-desktop/raw/main/Image/qrcode_1784783442708.jpg)

---

Tout le contenu est généré par l’IA

> 法语简写：**fr**，文件名 `README_fr.md`
> 需要继续输出阿拉伯语版本吗？
