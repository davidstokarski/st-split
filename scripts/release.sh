#!/usr/bin/env bash
# Builds universal (arm64 + x86_64) VST3 + AU, zips just the plugin bundles and publishes them as a GitHub Release.
#
#   scripts/release.sh            # version from CMakeLists.txt, uploads to GitHub
#   scripts/release.sh --no-upload
#
# Produces dist/ST-Split-<version>-macOS.zip and dist/ST-Split-macOS.zip (stable name used by the README download link).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PRODUCT="ST Split"
SLUG="ST-Split"
TARGET="STSplit"
VERSION="$(sed -nE 's/^project\([A-Za-z]+ VERSION ([0-9.]+).*/\1/p' CMakeLists.txt | head -1)"
UPLOAD=1
[[ "${1:-}" == "--no-upload" ]] && UPLOAD=0

BUILD="$ROOT/build-release"
DIST="$ROOT/dist"
STAGE="$DIST/$PRODUCT $VERSION"

echo "==> Building $PRODUCT $VERSION (universal)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DST_BUILD_STANDALONE=OFF -DST_COPY_AFTER_BUILD=OFF > /dev/null
cmake --build "$BUILD" --target "${TARGET}_VST3" "${TARGET}_AU"

ART="$BUILD/${TARGET}_artefacts/Release"
VST3="$ART/VST3/$PRODUCT.vst3"
AU="$ART/AU/$PRODUCT.component"

echo "==> Checking architectures"
lipo -info "$VST3/Contents/MacOS/$PRODUCT"
lipo -info "$AU/Contents/MacOS/$PRODUCT"

echo "==> Staging"
rm -rf "$STAGE"
mkdir -p "$STAGE"
ditto "$VST3" "$STAGE/$PRODUCT.vst3"
ditto "$AU" "$STAGE/$PRODUCT.component"
codesign --force --deep --sign - "$STAGE/$PRODUCT.vst3"
codesign --force --deep --sign - "$STAGE/$PRODUCT.component"

cat > "$STAGE/INSTALL.txt" <<TXT
$PRODUCT $VERSION — macOS (Apple Silicon + Intel)

1. Copy "$PRODUCT.vst3" to      ~/Library/Audio/Plug-Ins/VST3/
2. Copy "$PRODUCT.component" to ~/Library/Audio/Plug-Ins/Components/
3. These builds are ad-hoc signed (not notarised). If macOS blocks them, run once in Terminal:
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"$PRODUCT.vst3" ~/Library/Audio/Plug-Ins/Components/"$PRODUCT.component"
4. Restart your DAW (Logic: it rescans Audio Units on launch).
TXT

echo "==> Zipping"
VERSIONED="$DIST/$SLUG-$VERSION-macOS.zip"
STABLE="$DIST/$SLUG-macOS.zip"
rm -f "$VERSIONED" "$STABLE"
(cd "$DIST" && ditto -c -k --norsrc --noextattr --noqtn --keepParent "$PRODUCT $VERSION" "$VERSIONED")
cp "$VERSIONED" "$STABLE"
ls -lh "$VERSIONED" "$STABLE"

if [[ $UPLOAD -eq 1 ]]; then
    TAG="v$VERSION"
    echo "==> Publishing GitHub release $TAG"
    if gh release view "$TAG" > /dev/null 2>&1; then
        gh release upload "$TAG" "$VERSIONED" "$STABLE" --clobber
    else
        gh release create "$TAG" "$VERSIONED" "$STABLE" --title "$PRODUCT $VERSION" \
            --notes "Universal macOS build (Apple Silicon + Intel). Contains **$PRODUCT.vst3** and **$PRODUCT.component** — see INSTALL.txt inside the zip."
    fi
fi
echo "Done."
