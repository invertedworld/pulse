#!/bin/bash
# Sign, package, notarize and staple the Pulse plugin: AU, VST3 and AAX.
# The standalone app is built for development but never shipped.
# Prerequisites: See NOTARIZE_AND_INSTALLER.md. The AAX also needs aax-tools in
# ~/code/aax-tools (or MH_AAX_TOOLS=<path>) and the "signing" iLok.
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
BUILD_NUMBER="$(head -n 1 "$PROJECT_ROOT/build_number" | tr -d '[:space:]')"
# The build number keeps a rebuild from overwriting an installer that has
# shipped, and matches the Windows installer's name from aax-tools.
ARTIFACT_NAME="Pulse-${VERSION}-build${BUILD_NUMBER}"
AAX_TOOLS="${MH_AAX_TOOLS:-$HOME/code/aax-tools}"

# Paths to built artifacts
AU_PATH="$RELEASE_DIR/AU/Pulse.component"
VST3_PATH="$RELEASE_DIR/VST3/Pulse.vst3"
AAX_PATH="$RELEASE_DIR/AAX/Pulse.aaxplugin"

# --- CHECKS ---
if [ ! -d "$AU_PATH" ] || [ ! -d "$VST3_PATH" ] || [ ! -d "$AAX_PATH" ]; then
    echo "Error: Build artifacts not found. Run ./build.sh first."
    exit 1
fi

if [ ! -x "$AAX_TOOLS/aax-sign.sh" ]; then
    echo "Error: No aax-tools at $AAX_TOOLS (clone invertedworld/aax-tools there)."
    exit 1
fi
"$AAX_TOOLS/aax-sign.sh" --check pulse

# Checked before anything is signed, so stale credentials fail in seconds.
# shellcheck source=notary-auth.sh
source "$SCRIPT_DIR/notary-auth.sh"
if ! xcrun notarytool history "${NOTARY_AUTH[@]}" > /dev/null; then
    echo "Error: Apple refused the notary credentials ($NOTARY_DESC). See packaging/notary-auth.sh."
    exit 1
fi
echo "Notary credentials: $NOTARY_DESC"

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

# wraptool signs the AAX for Pro Tools and runs codesign itself, with the
# hardened runtime, so the AAX must not go through sign_bundle as well.
echo "=== Signing the AAX with PACE's tools ==="
"$AAX_TOOLS/aax-sign.sh" "$AAX_PATH" pulse

echo "=== Building component packages ==="
# pkgbuild --component already emits these non-relocatable (an empty <relocate/>
# with no bundles to search for), so each bundle lands exactly where it is sent.
pkgbuild --identifier "${BUNDLE_ID}.au.pkg" --version "$VERSION" \
    --component "$AU_PATH" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    "$PKG_DIR/Pulse.au.pkg"

pkgbuild --identifier "${BUNDLE_ID}.vst3.pkg" --version "$VERSION" \
    --component "$VST3_PATH" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "$PKG_DIR/Pulse.vst3.pkg"

pkgbuild --identifier "${BUNDLE_ID}.aax.pkg" --version "$VERSION" \
    --component "$AAX_PATH" \
    --install-location "/Library/Application Support/Avid/Audio/Plug-Ins" \
    "$PKG_DIR/Pulse.aax.pkg"

DIST_XML="$PKG_DIR/distribution_final.xml"
sed "s/VERSION_PLACEHOLDER/$VERSION/g" "$PKG_DIR/distribution.xml" > "$DIST_XML"

echo "=== Building final installer ==="
PKG_FINAL="$OUTPUT_DIR/${ARTIFACT_NAME}.pkg"

productbuild --distribution "$PKG_DIR/distribution_final.xml" \
    --package-path "$PKG_DIR" \
    --sign "$DEVELOPER_ID_INSTALLER" \
    --timestamp \
    "$PKG_FINAL"

echo "=== Notarizing ==="
xcrun notarytool submit "$PKG_FINAL" \
    "${NOTARY_AUTH[@]}" \
    --wait

echo "=== Stapling ==="
xcrun stapler staple "$PKG_FINAL"

# Cleanup intermediates
rm -f "$PKG_DIR/Pulse.au.pkg" "$PKG_DIR/Pulse.vst3.pkg" \
      "$PKG_DIR/Pulse.aax.pkg" "$PKG_DIR/distribution_final.xml"

echo ""
echo "Done. Installer: $OUTPUT_DIR/${ARTIFACT_NAME}.pkg"
