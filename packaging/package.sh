#!/bin/sh
# Usage: package.sh <binary> <platform> <tag> <outdir>
# Prints the path of the archive it wrote.
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: package.sh <binary> <platform> <tag> <outdir>" >&2
    exit 2
fi
bin=$1
platform=$2
tag=$3
mkdir -p "$4"
outdir=$(cd "$4" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)
name="signals-$tag-$platform"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$name"

case "$platform" in
    windows-*) exe=signals.exe ;;
    *) exe=signals ;;
esac
cp "$bin" "$stage/$name/$exe"
cp "$root/README.md" "$root/LICENSE" "$stage/$name/"
if [ -f "$root/README.ru.md" ]; then
    cp "$root/README.ru.md" "$stage/$name/"
fi

case "$platform" in
    windows-*)
        (cd "$stage" && 7z a -tzip "$outdir/$name.zip" "$name" > /dev/null)
        echo "$outdir/$name.zip"
        ;;
    *)
        mkdir -p "$stage/$name/man" "$stage/$name/completions"
        "$bin" --lang en man > "$stage/$name/man/signals.1"
        "$bin" --lang en completions bash > "$stage/$name/completions/signals.bash"
        "$bin" --lang en completions zsh > "$stage/$name/completions/_signals"
        "$bin" --lang en completions fish > "$stage/$name/completions/signals.fish"
        strip "$stage/$name/$exe"
        (cd "$stage" && tar -czf "$outdir/$name.tar.gz" "$name")
        echo "$outdir/$name.tar.gz"
        ;;
esac
