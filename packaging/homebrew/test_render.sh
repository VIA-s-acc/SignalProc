#!/bin/sh
# Usage: test_render.sh
# Renders the formula for an old-style (0.8.0) and a new-style release and checks the result.
set -u
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0
a=$(printf 'a%.0s' $(seq 64))
b=$(printf 'b%.0s' $(seq 64))
c=$(printf 'c%.0s' $(seq 64))

pass() { echo "PASS $1"; }
fail() {
    echo "FAIL $1: $2"
    failures=$((failures + 1))
}
has() { grep -qF -- "$2" "$tmp/$1.rb"; }
ruby_ok() { ! command -v ruby > /dev/null 2>&1 || ruby -c "$tmp/$1.rb" > /dev/null; }

printf '%s  signals-v0.8.0-macos-arm64.tar.gz\n%s  signals-v0.8.0-linux-x86_64.tar.gz\n%s  signals-v0.8.0-windows-x86_64.zip\n' "$a" "$b" "$c" > "$tmp/old.sums"
if ! sh "$here/render-formula.sh" v0.8.0 "$tmp/old.sums" > "$tmp/old.rb"; then
    fail old "render failed"
elif ! has old 'version "0.8.0"' || ! has old "signals-v0.8.0-macos-arm64.tar.gz" || ! has old "sha256 \"$a\""; then
    fail old "no macos-arm64 url, version or checksum"
elif ! has old "signals-v0.8.0-linux-x86_64.tar.gz" || ! has old "sha256 \"$b\""; then
    fail old "no linux url or checksum"
elif ! has old "depends_on arch: :arm64" || has old "man1.install"; then
    fail old "old release must be arm64-only on macOS and have no man page"
elif ! has old "signals --version"; then
    fail old "test block does not check the version"
elif ! ruby_ok old; then
    fail old "ruby -c rejects the formula"
else
    pass old
fi

printf '%s  signals-v9.9.9-macos-universal.tar.gz\n%s  signals-v9.9.9-linux-x86_64.tar.gz\n%s  signals-v9.9.9-windows-x86_64.zip\n' "$a" "$b" "$c" > "$tmp/new.sums"
if ! sh "$here/render-formula.sh" v9.9.9 "$tmp/new.sums" > "$tmp/new.rb"; then
    fail new "render failed"
elif ! has new 'version "9.9.9"' || ! has new "signals-v9.9.9-macos-universal.tar.gz" || ! has new "sha256 \"$a\""; then
    fail new "no macos-universal url, version or checksum"
elif has new "on_arm" || has new "depends_on arch"; then
    fail new "universal release must not be limited to arm64"
elif ! has new 'man1.install "man/signals.1"' || ! has new "fish_completion.install"; then
    fail new "no man page or completions"
elif ! has new '"check"'; then
    fail new "test block does not run signals check"
elif ! ruby_ok new; then
    fail new "ruby -c rejects the formula"
else
    pass new
fi

printf '%s  signals-v1.0.0-macos-universal.tar.gz\n' "$a" > "$tmp/nolinux.sums"
if sh "$here/render-formula.sh" v1.0.0 "$tmp/nolinux.sums" > /dev/null 2>&1; then
    fail no-linux "render succeeded without a linux archive"
else
    pass no-linux
fi

[ "$failures" -eq 0 ]
