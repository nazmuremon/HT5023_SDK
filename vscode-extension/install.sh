#!/usr/bin/env bash
# Install the HT5023 VS Code extension for the current user.
#
#   ./install.sh              # install / reinstall, then tell you to reload
#   ./install.sh --uninstall  # remove it
#
# The extension is plain JavaScript with no dependencies, so "installing" is a
# copy into the extensions folder. No npm, no build step, no marketplace.
set -uo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
NAME="$(python3 -c "import json;print(json.load(open('$HERE/package.json'))['name'])")"
PUB="$(python3 -c "import json;print(json.load(open('$HERE/package.json'))['publisher'])")"
VER="$(python3 -c "import json;print(json.load(open('$HERE/package.json'))['version'])")"
DEST_ROOT="${VSCODE_EXTENSIONS:-$HOME/.vscode/extensions}"
DEST="$DEST_ROOT/$PUB.$NAME-$VER"

if [ "${1:-}" = "--uninstall" ]; then
    rm -rf "$DEST_ROOT/$PUB.$NAME-"*
    echo "Removed $PUB.$NAME from $DEST_ROOT"
    echo "Reload VS Code (Ctrl+Shift+P -> Developer: Reload Window)."
    exit 0
fi

if [ ! -d "$DEST_ROOT" ]; then
    echo "VS Code extensions folder not found: $DEST_ROOT" >&2
    echo "Set VSCODE_EXTENSIONS if yours lives somewhere else." >&2
    exit 1
fi

# Drop older versions so VS Code does not load two copies.
rm -rf "$DEST_ROOT/$PUB.$NAME-"*
mkdir -p "$DEST"
cp -r "$HERE/package.json" "$HERE/extension.js" "$DEST/"
[ -d "$HERE/images" ] && cp -r "$HERE/images" "$DEST/"
[ -f "$HERE/README.md" ] && cp "$HERE/README.md" "$DEST/"

echo "Installed $PUB.$NAME $VER -> $DEST"
echo
echo "Reload VS Code to pick it up:"
echo "    Ctrl+Shift+P  ->  Developer: Reload Window"
echo
echo "Then: Ctrl+Shift+P -> 'HT5023' to see the commands, or use the"
echo "status-bar buttons at the bottom left."
