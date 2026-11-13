#!/bin/sh
# Usage: render-formula.sh <tag> <SHA256SUMS>
# Prints the Homebrew formula for that release.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: render-formula.sh <tag> <SHA256SUMS>" >&2
    exit 2
fi
tag=$1
sums=$2
here=$(cd "$(dirname "$0")" && pwd)
url="https://github.com/VIA-s-acc/SignalProc/releases/download/$tag"

sha_of() { awk -v f="signals-$tag-$1.tar.gz" '$2 == f || $2 == "*" f { print $1 }' "$sums"; }
linux=$(sha_of linux-x86_64)
universal=$(sha_of macos-universal)
arm=$(sha_of macos-arm64)
if [ -z "$linux" ]; then
    echo "no linux-x86_64 archive in $sums" >&2
    exit 1
fi

if [ -n "$universal" ]; then
    macos="    url \"$url/signals-$tag-macos-universal.tar.gz\"
    sha256 \"$universal\""
    extras='    man1.install "man/signals.1"
    bash_completion.install "completions/signals.bash" => "signals"
    zsh_completion.install "completions/_signals"
    fish_completion.install "completions/signals.fish"'
elif [ -n "$arm" ]; then
    macos="    depends_on arch: :arm64

    on_arm do
      url \"$url/signals-$tag-macos-arm64.tar.gz\"
      sha256 \"$arm\"
    end"
    extras=""
else
    echo "no macOS archive in $sums" >&2
    exit 1
fi

MACOS=$macos EXTRAS=$extras VERSION=${tag#v} TAG=$tag URL=$url SHA_LINUX=$linux awk '
    $0 == "@MACOS@" { print ENVIRON["MACOS"]; next }
    $0 == "@EXTRAS@" { if (ENVIRON["EXTRAS"] != "") print ENVIRON["EXTRAS"]; next }
    {
        gsub(/@VERSION@/, ENVIRON["VERSION"])
        gsub(/@TAG@/, ENVIRON["TAG"])
        gsub(/@URL@/, ENVIRON["URL"])
        gsub(/@SHA_LINUX@/, ENVIRON["SHA_LINUX"])
        print
    }
' "$here/signals.rb.in"
