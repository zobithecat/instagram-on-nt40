#!/usr/bin/env bash
# tools/serve-ftp.sh — host FTP server to push each build into the guest
# (setup.md §4). Serves ./dist; in NT4: `ftp` -> open 10.0.2.2 2121 -> binary -> get app.exe
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p dist
echo "serving ./dist on ftp://0.0.0.0:2121 (anonymous, write enabled)"
echo "in NT4:  ftp  ->  open 10.0.2.2 2121  ->  binary  ->  get app.exe"
exec python3 -m pyftpdlib -p 2121 -w -d ./dist
