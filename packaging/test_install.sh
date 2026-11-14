#!/bin/sh
# Usage: test_install.sh <binary>
# Builds a fake release from <binary> and runs install.sh against it the way
# `curl ... | sh` does: the script arrives on stdin.
set -u

if [ "$#" -ne 1 ]; then
    echo "usage: test_install.sh <binary>" >&2
    exit 2
fi
bin=$1
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0
tag=v9.9.9

checksum() {
    if command -v sha256sum > /dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}

case "$(uname -s)" in
    Darwin) platform=macos-universal ;;
    *) platform=linux-x86_64 ;;
esac

make_release() {
    mkdir -p "$1/$tag"
    sh "$root/packaging/package.sh" "$bin" "$2" "$tag" "$1/$tag" > /dev/null 2>&1 || return 1
    (cd "$1/$tag" && checksum ./*.tar.gz | sed 's# \./# #' > SHA256SUMS)
}

pass() { echo "PASS $1"; }
fail() {
    echo "FAIL $1: $2"
    sed 's/^/    | /' "$tmp/$1.log" 2> /dev/null
    failures=$((failures + 1))
}

run() {
    name=$1
    home=$2
    release=$3
    shift 3
    mkdir -p "$home"
    env -u XDG_DATA_HOME -u XDG_CONFIG_HOME HOME="$home" SIGNALS_RELEASE_URL="file://$release" SIGNALS_VERSION="$tag" "$@" sh < "$root/install.sh" > "$tmp/$name.log" 2>&1
}

logged() { grep -q -- "$2" "$tmp/$1.log"; }

make_release "$tmp/rel" "$platform" || { echo "FAIL setup: package.sh did not build $platform"; exit 1; }
want=$("$bin" --version < /dev/null)

home="$tmp/home-fresh"
if ! run fresh "$home" "$tmp/rel"; then
    fail fresh "exit code is not 0"
elif [ "$("$home/.local/bin/signals" --version < /dev/null)" != "$want" ]; then
    fail fresh "installed binary does not print '$want'"
elif [ ! -f "$home/.local/share/man/man1/signals.1" ]; then
    fail fresh "no man page"
elif [ ! -f "$home/.local/share/bash-completion/completions/signals" ] || [ ! -f "$home/.config/fish/completions/signals.fish" ]; then
    fail fresh "no bash or fish completion"
elif ! logged fresh "PATH"; then
    fail fresh "no hint about PATH"
else
    pass fresh
fi

if ! run again "$home" "$tmp/rel"; then
    fail again "exit code is not 0"
elif ! logged again "already installed"; then
    fail again "second run does not say 'already installed'"
else
    pass again
fi

home="$tmp/home-update"
mkdir -p "$home/.local/bin"
printf '#!/bin/sh\necho "signals 0.0.1"\n' > "$home/.local/bin/signals"
chmod 755 "$home/.local/bin/signals"
if ! run update "$home" "$tmp/rel"; then
    fail update "exit code is not 0"
elif ! logged update "updated 0.0.1 "; then
    fail update "no 'updated 0.0.1 ...' line"
elif [ "$("$home/.local/bin/signals" --version < /dev/null)" != "$want" ]; then
    fail update "old binary was not replaced"
else
    pass update
fi

cp -R "$tmp/rel" "$tmp/rel-bad"
sed 's/^[0-9a-f]*/0000000000000000000000000000000000000000000000000000000000000000/' "$tmp/rel/$tag/SHA256SUMS" > "$tmp/rel-bad/$tag/SHA256SUMS"
home="$tmp/home-bad"
if run bad-sum "$home" "$tmp/rel-bad"; then
    fail bad-sum "exit code is 0"
elif ! logged bad-sum "checksum"; then
    fail bad-sum "error does not mention the checksum"
elif [ -e "$home/.local/bin/signals" ]; then
    fail bad-sum "binary was installed anyway"
else
    pass bad-sum
fi

target="$tmp/dir with space"
if ! run space "$tmp/home-space" "$tmp/rel" SIGNALS_INSTALL_DIR="$target"; then
    fail space "exit code is not 0"
elif [ ! -x "$target/signals" ]; then
    fail space "binary is not in '$target'"
else
    pass space
fi

mkdir -p "$tmp/flags"
if ! env HOME="$tmp/home-flags" SIGNALS_RELEASE_URL="file://$tmp/rel" sh -s -- --version "$tag" --dir "$tmp/flags" < "$root/install.sh" > "$tmp/flags.log" 2>&1; then
    fail flags "exit code is not 0"
elif [ ! -x "$tmp/flags/signals" ]; then
    fail flags "--dir was ignored"
else
    pass flags
fi

mkdir -p "$tmp/fakebin"
# shellcheck disable=SC2016
printf '#!/bin/sh\ncase "$1" in -m) echo amd64 ;; *) echo FreeBSD ;; esac\n' > "$tmp/fakebin/uname"
chmod 755 "$tmp/fakebin/uname"
if run unsupported "$tmp/home-bsd" "$tmp/rel" PATH="$tmp/fakebin:$PATH"; then
    fail unsupported "exit code is 0"
elif ! logged unsupported "build from source"; then
    fail unsupported "error does not point to building from source"
else
    pass unsupported
fi

if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ]; then
    make_release "$tmp/rel-arm" macos-arm64 || { echo "FAIL setup: package.sh did not build macos-arm64"; exit 1; }
    if ! run arm64-fallback "$tmp/home-arm" "$tmp/rel-arm"; then
        fail arm64-fallback "exit code is not 0"
    elif [ ! -x "$tmp/home-arm/.local/bin/signals" ]; then
        fail arm64-fallback "binary was not installed"
    else
        pass arm64-fallback
    fi
fi

[ "$failures" -eq 0 ]
