#!/bin/zsh
set -euo pipefail
cd "${0:A:h}"
if [[ -x /opt/homebrew/bin/python3 ]]; then
    exec /opt/homebrew/bin/python3 tools/macos/black.py run --game-mode --metal --audio-engine dsp --dsp-headroom 6 "$@"
fi
exec python3 tools/macos/black.py run --game-mode --metal --audio-engine dsp --dsp-headroom 6 "$@"
