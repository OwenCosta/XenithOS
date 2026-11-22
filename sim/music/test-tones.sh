#!/bin/bash
# Music's sound, end to end: makes the Tones album (a WAV, a FLAC, an MP3 and an Ogg, each a steady
# tone at its own sample rate), plays it in the simulator, and checks the recording for pitch, order,
# length, gaps and clicks (check-tones.py). Needs ffmpeg with lame and vorbis, and python3.
#   sim/music/test-tones.sh            exit 0 when it all holds
set -e
here="$(cd "$(dirname "$0")" && pwd)"
tones="$HOME/.xsim-tones"
"$here/make-tones.sh" "$tones" > /dev/null
XSIM_BIN=xmusic XSIM_MUSIC="$tones" "$here/../run.sh" "$here/../scripts/music_tones.txt" music_tones 2>&1 \
    | grep -E "runtime error|Sanitizer|dropouts" || true
python3 "$here/check-tones.py" "$here/../out/music_tones/audio.wav"
