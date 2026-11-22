#!/bin/bash
# Four steady tones, one per format and each at its own rate, tagged as one album in order:
#   1 WAV 44.1 kHz 500 Hz, 2 FLAC 48 kHz 750 Hz, 3 MP3 44.1 kHz 1000 Hz, 4 Ogg 32 kHz 1250 Hz.
# Three seconds each at half volume, so the recording can be checked for pitch and gaps.
set -e
out="${1:-$HOME/.xsim-tones}"
rm -rf "$out"
mkdir -p "$out/Tones"
cd "$out/Tones"
make_tone() {   # make_tone <Hz> <rate> <title> <track> <codec args...> <file>
    local hz=$1 rate=$2 title=$3 track=$4
    shift 4
    local file="${@: -1}"
    local codec=("${@:1:$#-1}")
    ffmpeg -loglevel error -f lavfi -i "sine=frequency=$hz:sample_rate=$rate:duration=3" -af volume=0.5 -ac 2 \
        "${codec[@]}" -metadata title="$title" -metadata artist="Tone Maker" -metadata album="Tones" \
        -metadata album_artist="Tone Maker" -metadata track="$track" "$file"
}
make_tone 500 44100 Five 1 -c:a pcm_s16le "1 five.wav"
make_tone 750 48000 Seven 2 -c:a flac "2 seven.flac"
make_tone 1000 44100 Thousand 3 -c:a libmp3lame -b:a 192k "3 thousand.mp3"
make_tone 1250 32000 Twelve 4 -c:a libvorbis -q:a 6 "4 twelve.ogg"
ls -la
