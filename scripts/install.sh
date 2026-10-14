#!/usr/bin/env sh
# Installs unreal-cli from GitHub Releases after verifying SHA256SUMS.
# UNREAL_CLI_VERSION=v0.1.0 pins a release; UNREAL_CLI_REPO overrides owner/name.
set -eu
REPO="${UNREAL_CLI_REPO:-KiritoShikahara/unreal-cli}"
VERSION="${UNREAL_CLI_VERSION:-latest}"
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) ASSET=unreal-cli-aarch64-apple-darwin.tar.gz ;;
  Darwin-*) ASSET=unreal-cli-x86_64-apple-darwin.tar.gz ;;
  Linux-x86_64) ASSET=unreal-cli-x86_64-unknown-linux-gnu.tar.gz ;;
  *) echo "unsupported platform: $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac
if [ "$VERSION" = latest ]; then BASE="https://github.com/$REPO/releases/latest/download"; else BASE="https://github.com/$REPO/releases/download/$VERSION"; fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL "$BASE/SHA256SUMS" -o "$TMP/SHA256SUMS"
curl -fsSL "$BASE/$ASSET" -o "$TMP/$ASSET"
EXPECTED="$(grep " $ASSET\$" "$TMP/SHA256SUMS" | cut -d' ' -f1)"
if command -v sha256sum >/dev/null 2>&1; then ACTUAL="$(sha256sum "$TMP/$ASSET" | cut -d' ' -f1)"; else ACTUAL="$(shasum -a 256 "$TMP/$ASSET" | cut -d' ' -f1)"; fi
if [ -z "$EXPECTED" ] || [ "$EXPECTED" != "$ACTUAL" ]; then
  echo "checksum mismatch for $ASSET; nothing was installed" >&2
  exit 1
fi
# INSTALL-STEP
DEST="$HOME/.local/share/unreal-cli/bin"
mkdir -p "$DEST" "$HOME/.local/bin"
tar -xzf "$TMP/$ASSET" -C "$TMP"
install -m 0755 "$TMP/unreal-cli" "$DEST/unreal-cli"
ln -sf "$DEST/unreal-cli" "$HOME/.local/bin/unreal-cli"
echo "installed: $HOME/.local/bin/unreal-cli"
