#!/bin/sh
# Installs Signals from a GitHub release into your home folder.
#
#   curl -fsSL https://raw.githubusercontent.com/VIA-s-acc/SignalProc/main/install.sh | sh
#   curl -fsSL https://raw.githubusercontent.com/VIA-s-acc/SignalProc/main/install.sh | sh -s -- --version v0.9.0 --dir ~/bin
#
# Environment: SIGNALS_VERSION (tag, default: latest), SIGNALS_INSTALL_DIR
# (default: ~/.local/bin), SIGNALS_RELEASE_URL (where release files live).
#
# The script arrives on stdin under `curl | sh`, so nothing here may read stdin.
set -eu

repo="VIA-s-acc/SignalProc"
version="${SIGNALS_VERSION:-}"
dir="${SIGNALS_INSTALL_DIR:-$HOME/.local/bin}"
base="${SIGNALS_RELEASE_URL:-https://github.com/$repo/releases/download}"

say() { printf '%s\n' "$*"; }
die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            [ "$#" -ge 2 ] || die "--version needs a tag, for example v0.9.0"
            version=$2
            shift 2
            ;;
        --version=*)
            version=${1#*=}
            shift
            ;;
        --dir)
            [ "$#" -ge 2 ] || die "--dir needs a folder"
            dir=$2
            shift 2
            ;;
        --dir=*)
            dir=${1#*=}
            shift
            ;;
        -h | --help)
            say "usage: install.sh [--version vX.Y.Z] [--dir DIR]"
            exit 0
            ;;
        *) die "unknown option: $1" ;;
    esac
done

have() { command -v "$1" > /dev/null 2>&1; }

if have curl; then
    fetch() { curl -fsSL -o "$2" "$1"; }
elif have wget; then
    fetch() { wget -q -O "$2" "$1"; }
else
    die "curl or wget is required"
fi
if have sha256sum; then
    sha() { sha256sum "$1" | cut -d ' ' -f 1; }
elif have shasum; then
    sha() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
else
    die "sha256sum or shasum is required"
fi
have tar || die "tar is required"

os=$(uname -s)
arch=$(uname -m)
unsupported() {
    die "there is no prebuilt Signals for $os $arch; build from source: https://github.com/$repo#build-from-source"
}
case "$os" in
    Darwin)
        platforms="macos-universal"
        if [ "$arch" = arm64 ]; then platforms="macos-universal macos-arm64"; fi
        ;;
    Linux)
        case "$arch" in
            x86_64 | amd64) platforms="linux-x86_64" ;;
            *) unsupported ;;
        esac
        ;;
    *) unsupported ;;
esac

if [ -z "$version" ]; then
    if have curl; then
        url=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$repo/releases/latest") || url=""
        version=${url##*/tag/}
    else
        version=$(wget -qO- "https://api.github.com/repos/$repo/releases/latest" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n 1)
    fi
    case "$version" in
        v[0-9]*) ;;
        *) die "cannot find the latest release of $repo" ;;
    esac
fi

tmp=$(mktemp -d 2> /dev/null || mktemp -d -t signals)
trap 'rm -rf "$tmp"' EXIT INT TERM

fetch "$base/$version/SHA256SUMS" "$tmp/SHA256SUMS" || die "cannot download SHA256SUMS of $version; is the version right?"
file=""
expected=""
for platform in $platforms; do
    candidate="signals-$version-$platform.tar.gz"
    expected=$(awk -v f="$candidate" '$2 == f || $2 == "*" f { print $1 }' "$tmp/SHA256SUMS")
    if [ -n "$expected" ]; then
        file=$candidate
        break
    fi
done
[ -n "$file" ] || die "release $version has no build for $os $arch"

say "Downloading $file"
fetch "$base/$version/$file" "$tmp/$file" || die "cannot download $file"
[ "$(sha "$tmp/$file")" = "$expected" ] || die "checksum mismatch for $file; nothing was installed"
tar -xzf "$tmp/$file" -C "$tmp"
src="$tmp/${file%.tar.gz}"
[ -f "$src/signals" ] || die "$file has no signals binary"

target="$dir/signals"
old=""
if [ -f "$target" ] && [ "$(sha "$target")" = "$(sha "$src/signals")" ]; then
    same=1
else
    same=0
    if [ -x "$target" ]; then old=$("$target" --version < /dev/null 2> /dev/null | sed -n '1s/^signals //p') || old=""; fi
    mkdir -p "$dir"
    cp "$src/signals" "$target.new"
    chmod 755 "$target.new"
    mv -f "$target.new" "$target"
fi
if [ "$os" = Darwin ] && have xattr; then xattr -d com.apple.quarantine "$target" 2> /dev/null || true; fi
new=$("$target" --version < /dev/null | sed -n '1s/^signals //p')

data="${XDG_DATA_HOME:-$HOME/.local/share}"
if [ -f "$src/man/signals.1" ]; then
    mkdir -p "$data/man/man1"
    cp "$src/man/signals.1" "$data/man/man1/signals.1"
fi
completions=0
if [ -d "$src/completions" ]; then
    completions=1
    mkdir -p "$data/bash-completion/completions"
    cp "$src/completions/signals.bash" "$data/bash-completion/completions/signals"
    fish="${XDG_CONFIG_HOME:-$HOME/.config}/fish/completions"
    mkdir -p "$fish"
    cp "$src/completions/signals.fish" "$fish/signals.fish"
fi

if [ "$same" = 1 ]; then
    say "Signals $new is already installed in $dir"
elif [ -n "$old" ] && [ "$old" != "$new" ]; then
    say "Signals updated $old → $new in $dir"
else
    say "Signals $new installed to $target"
fi

shell_name=$(basename "${SHELL:-sh}")
case ":$PATH:" in
    *":$dir:"*) ;;
    *)
        say ""
        say "$dir is not in PATH yet. Add this line to your shell profile and open a new terminal:"
        if [ "$shell_name" = fish ]; then
            say "  fish_add_path \"$dir\""
        else
            say "  export PATH=\"$dir:\$PATH\""
        fi
        ;;
esac
if [ "$completions" = 1 ] && [ "$shell_name" = zsh ]; then
    say ""
    say "zsh completion: add 'source <(signals completions zsh)' to ~/.zshrc after compinit."
fi
say ""
say "Next: run 'signals' to open the interface, or 'signals learn' for the learning path."
