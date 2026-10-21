#!/bin/bash
# This file is part of hl2270dw-macos.
#
# Copyright 2026 Jon Marler
#
# hl2270dw-macos is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# Builds the universal binaries, runs the tests, and creates the installer
# package in dist/.
#
# Signing uses the first valid "Developer ID Application" and "Developer ID
# Installer" identities in your keychain. Override them with:
#   APP_IDENTITY=<name or SHA-1>        INSTALLER_IDENTITY=<name or SHA-1>
# Set APP_IDENTITY=- to sign the binaries ad hoc and INSTALLER_IDENTITY=- to
# leave the package unsigned (for local builds without certificates).
#
# Notarization runs when NOTARY_PROFILE names a profile created with
#   xcrun notarytool store-credentials <profile>

set -euo pipefail

cd "$(dirname "$0")/.."
SRC=$PWD
VERSION=$(sed -n 's/^set(HL2270DW_VERSION "\(.*\)")$/\1/p' CMakeLists.txt)
IDENTIFIER="io.github.jmarler.hl2270dw-macos"
BUILD="$SRC/build-release"
DIST="$SRC/dist"
PPD_NAME="Brother HL-2270DW series hl2270dw-macos.gz"

if [ -z "${APP_IDENTITY:-}" ]; then
  APP_IDENTITY=$(security find-identity -v -p codesigning \
    | awk '/"Developer ID Application/ { print $2; exit }')
fi
if [ -z "${INSTALLER_IDENTITY:-}" ]; then
  INSTALLER_IDENTITY=$(security find-identity -v \
    | awk '/"Developer ID Installer/ { print $2; exit }')
fi
APP_IDENTITY=${APP_IDENTITY:--}
INSTALLER_IDENTITY=${INSTALLER_IDENTITY:--}

echo "==> Building hl2270dw-macos $VERSION"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --parallel
(cd "$BUILD" && ctest --output-on-failure)

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ROOT="$STAGE/root"
APP_DIR="$ROOT/Library/Printers/hl2270dw"
PPD_DIR="$ROOT/Library/Printers/PPDs/Contents/Resources"
install -d -m 755 "$APP_DIR/Filter" "$PPD_DIR"
install -m 755 "$BUILD/rastertohl2270dw" "$BUILD/commandtohl2270dw" \
  "$APP_DIR/Filter/"
install -m 755 "$SRC/macos/uninstall.sh" "$APP_DIR/uninstall.sh"
install -m 644 "$SRC/COPYING" "$APP_DIR/COPYING"
install -m 644 "$SRC/README.md" "$APP_DIR/README.md"
gzip -9 -n -c "$BUILD/ppd/hl2270dw.ppd" > "$PPD_DIR/$PPD_NAME"
chmod 644 "$PPD_DIR/$PPD_NAME"

echo "==> Signing binaries (identity: $APP_IDENTITY)"
TIMESTAMP=--timestamp  # Required for notarization.
[ "$APP_IDENTITY" = "-" ] && TIMESTAMP=--timestamp=none
for f in rastertohl2270dw commandtohl2270dw; do
  codesign --force --options runtime "$TIMESTAMP" \
    --identifier "$IDENTIFIER.$f" --sign "$APP_IDENTITY" "$APP_DIR/Filter/$f"
  codesign --verify --strict "$APP_DIR/Filter/$f"
done

echo "==> Building package"
mkdir -p "$DIST"
pkgbuild --root "$ROOT" \
  --identifier "$IDENTIFIER" \
  --version "$VERSION" \
  --install-location / \
  --ownership recommended \
  "$STAGE/hl2270dw-component.pkg" >/dev/null

RES="$STAGE/resources"
mkdir -p "$RES"
cp "$SRC/COPYING" "$RES/COPYING.txt"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@IDENTIFIER@/$IDENTIFIER/g" \
  "$SRC/macos/distribution.xml.in" > "$STAGE/distribution.xml"

PKG="$DIST/hl2270dw-macos-$VERSION.pkg"
SIGN_ARGS=()
if [ "$INSTALLER_IDENTITY" != "-" ]; then
  echo "==> Signing package (identity: $INSTALLER_IDENTITY)"
  SIGN_ARGS=(--sign "$INSTALLER_IDENTITY" --timestamp)
fi
productbuild --distribution "$STAGE/distribution.xml" \
  --resources "$RES" \
  --package-path "$STAGE" \
  "${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}" \
  "$PKG" >/dev/null

if [ -n "${NOTARY_PROFILE:-}" ]; then
  echo "==> Notarizing"
  xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$PKG"
fi

echo "==> Created $PKG"
pkgutil --check-signature "$PKG" | sed -n '1,3p' || true
