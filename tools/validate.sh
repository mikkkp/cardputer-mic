#!/usr/bin/env bash
# Cardputer Mic 门禁。上游门禁硬编码了 mic_clips.bin / demo_*.c / 旧镜像名，
# 砍资源换 BSP 后不能直接照搬，故按方案 §3.5 重写为精简版。
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() (
    local test_dir
    test_dir="$(mktemp -d /tmp/cardputer-mic-host-tests.XXXXXX)"
    trap 'case "${test_dir}" in /tmp/cardputer-mic-host-tests.*) rm -rf -- "${test_dir}" ;; esac' EXIT

    # 与平台无关的协议 / 编码 / 省电状态机测试（上游零改动复用）。
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_mic_protocol.c main/mic_protocol.c -o "${test_dir}/test_mic_protocol"
    "${test_dir}/test_mic_protocol" "${test_dir}/mic-wire.bin"

    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_mic_adpcm.c main/mic_adpcm.c -o "${test_dir}/test_mic_adpcm"
    "${test_dir}/test_mic_adpcm" "${test_dir}/mic-adpcm.bin"

    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_mic_screen.c main/mic_screen.c -o "${test_dir}/test_mic_screen"
    "${test_dir}/test_mic_screen"

    # Python 测试通过 tests/ref/ 里的协议参考实现校验 C 端输出。
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_mic_adpcm.py "${test_dir}/mic-adpcm.bin"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_mic_bridge.py "${test_dir}/mic-wire.bin"

    # 本工程新增的纯逻辑测试：引脚表一致性、键盘映射、电量换算。
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_bsp_pins.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/include \
        tests/test_bsp_keymap.c components/bsp/src/bsp_keymap.c \
        -o "${test_dir}/test_bsp_keymap"
    "${test_dir}/test_bsp_keymap"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/include \
        tests/test_bsp_battery_soc.c components/bsp/src/bsp_battery_soc.c \
        -o "${test_dir}/test_bsp_battery_soc"
    "${test_dir}/test_bsp_battery_soc"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/include \
        tests/test_bsp_tca8418.c components/bsp/src/bsp_tca8418.c \
        -o "${test_dir}/test_bsp_tca8418"
    "${test_dir}/test_bsp_tca8418"

    # UI 渲染回归：主机侧把界面渲成 PPM，防止字形/字节序/布局回退成空屏或白底。
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/host_shim -Imain \
        tests/test_ui_render.c main/mic_ui_stub.c main/mic_font.c \
        -o "${test_dir}/test_ui_render"
    "${test_dir}/test_ui_render" "${test_dir}/ui.ppm" 1 78 >/dev/null
    "${test_dir}/test_ui_render" "${test_dir}/ui_pair.ppm" 1 78 384921 >/dev/null
    "${test_dir}/test_ui_render" "${test_dir}/ui_rec.ppm" 3 78 >/dev/null
    "${test_dir}/test_ui_render" "${test_dir}/ui_quiet.ppm" 4 78 >/dev/null
    "${test_dir}/test_ui_render" "${test_dir}/ui_unknown.ppm" 1 -2 >/dev/null
    "${test_dir}/test_ui_render" "${test_dir}/ui_zero.ppm" 1 0 >/dev/null
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_ui_render.py "${test_dir}"

    echo "Host tests: PASS"
)

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py 不可用，请先 source ~/esp/esp-idf/export.sh" >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/cardputer-mic-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/cardputer-mic-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/cardputer-mic-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"

    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/cardputer-mic-full.bin" \
        "${repo_root}/build/cardputer-mic-full.bin"
    # 保留 elf/map，便于按串口崩溃地址回溯（方案 §3.5）。
    for artifact in cardputer-mic.elf cardputer-mic.map; do
        if [[ -f "${validation_build_dir}/${artifact}" ]]; then
            install -m 0644 "${validation_build_dir}/${artifact}" \
                "${repo_root}/build/${artifact}"
        fi
    done
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
