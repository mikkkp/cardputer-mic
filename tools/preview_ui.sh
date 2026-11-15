#!/bin/bash
# 主机侧 UI 预览：不烧录就能看界面效果。
# 用法：tools/preview_ui.sh [输出目录，默认 /tmp/mic-ui-preview]
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
out="${1:-/tmp/mic-ui-preview}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Itests/host_shim -Imain \
    tests/test_ui_render.c main/mic_ui_stub.c main/mic_font.c -o "$out/ui_preview"
"$out/ui_preview" "$out/ready.ppm" 1 78
"$out/ui_preview" "$out/record.ppm" 3 78
"$out/ui_preview" "$out/pairing.ppm" 1 78 384921
"$out/ui_preview" "$out/offline.ppm" 0 -1
"$out/ui_preview" "$out/lowbat.ppm" 1 12
for f in ready record pairing offline lowbat; do
    sips -s format png "$out/$f.ppm" --out "$out/$f.png" >/dev/null
    sips -z 405 720 "$out/$f.png" --out "$out/${f}_x3.png" >/dev/null
done
echo "预览已生成：$out/*_x3.png"
