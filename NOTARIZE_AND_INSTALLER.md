# Notarization and Installer Setup for Pulse

This guide walks you through creating Apple Developer credentials, notarizing the Pulse plugin, and building a `.pkg` installer. It follows the same process as the other Mark Hammond plugins.

## Prerequisites

- Apple Developer Program membership ($99/year)
- Build the plugin first: `./build.sh` or `cmake --build build --config Release`

---

## Step 1: Create Certificates

You need **two** certificates, both created at [developer.apple.com/account/resources](https://developer.apple.com/account/resources).

**Note:** Only the Account Holder of your team can create Developer ID certificates.

### 1a. Create a Certificate Signing Request (CSR)

1. Open **Keychain Access** (`/Applications/Utilities`)
2. Menu: **Keychain Access → Certificate Assistant → Request a Certificate from a Certificate Authority**
3. Enter your email and a key name (e.g. "Pulse Dev Key")
4. Select **"Saved to disk"** → Continue → save the `.certSigningRequest`

### 1b. Developer ID Application

Signs apps and plugins (.app, .component, .vst3).

1. Certificates, Identifiers & Profiles → **Certificates** → **+**
2. Under **Software**, select **Developer ID** → **Developer ID Application** → Continue
3. Upload your CSR → download the `.cer` → double-click to install in Keychain

### 1c. Developer ID Installer

Signs the installer `.pkg`.

1. Create a **new CSR** → Certificates → **+** → **Developer ID** → **Developer ID Installer**
2. Upload CSR → download and install the `.cer`

### 1d. Verify in Keychain

In Keychain Access → **My Certificates** you should see:

- `Developer ID Application: Your Name (TEAM_ID)`
- `Developer ID Installer: Your Name (TEAM_ID)`

Find your **Team ID** at [developer.apple.com/account](https://developer.apple.com/account) → Membership.

---

## Step 2: Notarization Credentials

`packaging/notary-auth.sh` finds the credentials. By default it uses the App Store
Connect API key at `~/.appstoreconnect/private_keys/AuthKey_<key id>.p8`, which
does not go stale as keychain profiles holding an app-specific password do. Set
`KEYCHAIN_PROFILE=<name>` to use a stored notarytool profile instead.

---

## Step 3: Configure the Build Script

Edit `packaging/notarize_and_install.sh` and confirm:

- `APPLE_ID` – your Apple ID email
- `TEAM_ID` – 10-character Team ID
- `DEVELOPER_ID_APP` – full Developer ID Application cert name
- `DEVELOPER_ID_INSTALLER` – full Developer ID Installer cert name

See exact cert names with:

```bash
security find-identity -v -p codesigning
```

---

## Step 4: Run the Pipeline

```bash
cd packaging
./notarize_and_install.sh
```

The script will:

1. Sign the AU and VST3 with Developer ID Application (hardened runtime + secure timestamp), and the AAX with PACE's wraptool through `~/code/aax-tools/aax-sign.sh` (needs the "signing" iLok)
2. Build component `.pkg` files with `pkgbuild`
3. Create the final installer with `productbuild`
4. Submit for notarization with `notarytool --wait` and staple the ticket

Output: `Pulse-<version>-build<N>.pkg` in `packaging/output`, with the AU, VST3 and AAX. The standalone app is not included.

---

## Troubleshooting

### "The binary is not signed with a valid Developer ID certificate"
Use **Developer ID Application** for plugins, not Mac App Store certs. Re-sign with `--force` and `--timestamp`.

### "The signature does not include a secure timestamp"
Ensure `--timestamp` is passed to `codesign` (the script does).

### Notarization fails
```bash
source packaging/notary-auth.sh
xcrun notarytool log SUBMISSION_UUID "${NOTARY_AUTH[@]}"
```

### Verify signing
```bash
codesign --verify --deep --strict -v "path/to/Pulse.component"
codesign --verify --deep --strict -v "path/to/Pulse.vst3"
pkgutil --check-signature Pulse-1.2.1.pkg
```

---

## References

- [Create Developer ID certificates](https://developer.apple.com/help/account/certificates/create-developer-id-certificates/)
- [Notarizing macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [App-Specific Passwords](https://support.apple.com/en-us/102654)
