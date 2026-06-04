#!/bin/bash
# Shared helpers for bento_ttymidi test scripts.

set -euo pipefail

CLIENT="${BENTO_MIDI_CLIENT:-bento_ttymidi}"

find_port() {
    local label="$1"
    local cid="" port="" in_client=0
    while IFS= read -r line; do
        if [[ "$line" =~ ^client\ ([0-9]+):.*\'$CLIENT\' ]]; then
            cid="${BASH_REMATCH[1]}"
            in_client=1
            continue
        fi
        if [[ "$line" =~ ^client\ [0-9]+: ]]; then
            in_client=0
            cid=""
            continue
        fi
        # ALSA pads port names with trailing spaces inside the quotes.
        if [[ "$in_client" -eq 1 && "$line" =~ ^[[:space:]]+([0-9]+)[[:space:]]+\'$label[[:space:]]*\' ]]; then
            port="${BASH_REMATCH[1]}"
            echo "${cid}:${port}"
            return 0
        fi
    done < <(aconnect -l 2>/dev/null)
    return 1
}

find_client_id() {
    local cid=""
    while IFS= read -r line; do
        if [[ "$line" =~ ^client\ ([0-9]+):.*\'$CLIENT\' ]]; then
            cid="${BASH_REMATCH[1]}"
            echo "$cid"
            return 0
        fi
    done < <(aconnect -l 2>/dev/null)
    return 1
}

require_cmd() {
    local cmd="$1"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "[ERROR] Required command not found: $cmd"
        exit 1
    fi
}

require_bento_ttymidi() {
    local in_port out_port

    if ! systemctl is-active --quiet bento_ttymidi 2>/dev/null; then
        if ! pgrep -x bento_ttymidi >/dev/null 2>&1; then
            echo "[ERROR] bento_ttymidi is not running."
            echo "        Start with: systemctl start bento_ttymidi"
            exit 1
        fi
    fi

    in_port="$(find_port "TTY MIDI in" || true)"
    out_port="$(find_port "TTY MIDI out" || true)"

    if [ -z "$in_port" ] || [ -z "$out_port" ]; then
        echo "[ERROR] ALSA ports not found for $CLIENT"
        aconnect -l 2>/dev/null || true
        exit 1
    fi

    export BENTO_MIDI_IN_PORT="$in_port"
    export BENTO_MIDI_OUT_PORT="$out_port"
}

test_root() {
    cd "$(dirname "${BASH_SOURCE[1]}")/.."
}
