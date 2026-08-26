#!/bin/bash
# Sign, package, notarize and staple the Pulse plugin.
# Prerequisites: See NOTARIZE_AND_INSTALLER.md
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RELEASE_DIR="$BUILD_DIR/Pulse_artefacts/Release"
PKG_DIR="$SCRIPT_DIR"
OUTPUT_DIR="$SCRIPT_DIR/output"

# --- CONFIGURATION ---
# Get your Team ID from: https://developer.apple.com/account → Membership
TEAM_ID="U7KDB84YPD"

# Certificate names from Keychain. List with: security find-identity -v -p codesigning
DEVELOPER_ID_APP='Developer ID Application: Mark Hammond (U7KDB84YPD)'
DEVELOPER_ID_INSTALLER='Developer ID Installer: Mark Hammond (U7KDB84YPD)'

# Profile from: xcrun notarytool store-credentials "Pulse Notary" ...
KEYCHAIN_PROFILE="Pulse Notary"

# Plugin metadata (from CMakeLists)
BUNDLE_ID="com.MarkHammond.Pulse"
PRODUCT_NAME="Pulse"
# Read from CMakeLists rather than copied by hand: this drifted to 1.2.1 while
# the project moved on, and a stale version silently mislabels the installer.
VERSION="$(sed -n 's/^project(Pulse VERSION \([0-9.]*\)).*/\1/p' "$SCRIPT_DIR/../CMakeLists.txt")"
if [[ -z "$VERSION" ]]; then
    echo "Error: could not read the project version from CMakeLists.txt." >&2
    exit 1
fi
ARTIFACT_NAME="Pulse-${VERSION}"

# Paths to built artifacts
AU_PATH="$RELEASE_DIR/AU/Pulse.component"
VST3_PATH="$RELEASE_DIR/VST3/Pulse.vst3"
STANDALONE_PATH="$RELEASE_DIR/Standalone/Pulse.app"

# --- CHECKS ---
if [ ! -d "$AU_PATH" ] || [ ! -d "$VST3_PATH" ]; then
    echo "Error: Build artifacts not found. Run ./build.sh first."
    exit 1
fi

if [[ "$TEAM_ID" == *"YOUR_"* ]] || [[ "$DEVELOPER_ID_APP" == *"Your Name"* ]]; then
    echo "Error: Edit this script and set TEAM_ID, DEVELOPER_ID_APP, DEVELOPER_ID_INSTALLER."
    echo "Run: security find-identity -v -p codesigning"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
cd "$PKG_DIR"

echo "=== Signing plugins with Developer ID Application ==="
sign_bundle() {
    local path="$1"
    if [ -d "$path" ]; then
        echo "  Signing: $path"
        codesign --force --sign "$DEVELOPER_ID_APP" --options runtime --timestamp \
            --deep --strict -v "$path"
    fi
}

sign_bundle "$AU_PATH"
sign_bundle "$VST3_PATH"
sign_bundle "$STANDALONE_PATH"

echo "=== Building component packages ==="
# pkgbuild --component already emits these non-relocatable (an empty <relocate/>
# with no bundles to search for), so the destination chosen on the Destination
# Select pane is always honoured.
pkgbuild --identifier "${BUNDLE_ID}.au.pkg" --version "$VERSION" \
    --component "$AU_PATH" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    "$PKG_DIR/Pulse.au.pkg"

pkgbuild --identifier "${BUNDLE_ID}.vst3.pkg" --version "$VERSION" \
    --component "$VST3_PATH" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "$PKG_DIR/Pulse.vst3.pkg"

# Standalone: fix BundleIsRelocatable so it always installs to /Applications
if [ -d "$STANDALONE_PATH" ]; then
    pkgbuild --analyze --root "$(dirname "$STANDALONE_PATH")" "$PKG_DIR/standalone.plist"
    plutil -replace BundleIsRelocatable -bool NO "$PKG_DIR/standalone.plist"
    pkgbuild --identifier "${BUNDLE_ID}.app.pkg" --version "$VERSION" \
        --root "$(dirname "$STANDALONE_PATH")" \
        --component-plist "$PKG_DIR/standalone.plist" \
        --install-location "/Applications" \
        "$PKG_DIR/Pulse.app.pkg"
    echo "  Built standalone package"
else
    echo "  Skipping standalone (not built)"
fi

# Build distribution.xml (with or without standalone)
DIST_XML="$PKG_DIR/distribution_final.xml"
if [ -d "$STANDALONE_PATH" ]; then
    sed "s/VERSION_PLACEHOLDER/$VERSION/g" "$PKG_DIR/distribution.xml" > "$DIST_XML"
else
    # Plugins only - no standalone in installer
    sed "s/VERSION_PLACEHOLDER/$VERSION/g" "$PKG_DIR/distribution_plugins_only.xml" > "$DIST_XML"
fi

echo "=== Building final installer ==="
PKG_FINAL="$OUTPUT_DIR/${ARTIFACT_NAME}.pkg"

productbuild --distribution "$PKG_DIR/distribution_final.xml" \
    --package-path "$PKG_DIR" \
    --sign "$DEVELOPER_ID_INSTALLER" \
    --timestamp \
    "$PKG_FINAL"

echo "=== Notarizing ==="
xcrun notarytool submit "$PKG_FINAL" \
    --keychain-profile "$KEYCHAIN_PROFILE" \
    --wait

echo "=== Stapling ==="
xcrun stapler staple "$PKG_FINAL"

# Cleanup intermediates
rm -f "$PKG_DIR/Pulse.au.pkg" "$PKG_DIR/Pulse.vst3.pkg" \
      "$PKG_DIR/Pulse.app.pkg" "$PKG_DIR/standalone.plist" \
      "$PKG_DIR/distribution_final.xml"

echo ""
echo "Done. Installer: $OUTPUT_DIR/${ARTIFACT_NAME}.pkg"
