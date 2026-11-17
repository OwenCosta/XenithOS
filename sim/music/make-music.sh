#!/bin/bash
# Test music for the Music app, all synthesized here (no recordings, nothing copyrighted):
# little tunes from sine and square waves, as MP3, FLAC, Ogg Vorbis and WAV, with tags, covers
# inside the files and next to them, odd rates, accented names, and one broken file.
#   make-music.sh <folder>
set -e
out="${1:-$HOME/.xsim-music}"
rm -rf "$out"
mkdir -p "$out"
cd "$out"

# A tune: notes (Hz) a quarter second each, with a soft envelope, over a low drone.
tune() {   # tune <seconds> <base Hz> <step pattern seed>
    local secs=$1 base=$2 seed=$3
    echo "'0.22*sin(2*PI*($base*pow(2,(mod(floor(t*4)*$seed,7))/12))*t)*(1-0.6*mod(t*4,1))+0.06*sin(2*PI*($base/2)*t)'"
}

cover() {   # cover <file> <r> <g> <b> <text>
    python3 - "$@" <<'EOF'
import sys
from PIL import Image, ImageDraw
path, r, g, b, text = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
im = Image.new("RGB", (600, 600), (r, g, b))
d = ImageDraw.Draw(im)
d.ellipse([330, 330, 760, 760], fill=(min(r + 50, 255), min(g + 50, 255), min(b + 50, 255)))
d.rectangle([40, 40, 260, 70], fill=(240, 240, 240))
d.rectangle([40, 90, 180, 110], fill=(200, 200, 200))
d.text((40, 140), text, fill=(250, 250, 250))
im.save(path, quality=90)
EOF
}

# 1. MP3s, 44.1 kHz stereo, ID3v2.3 tags, the cover inside the first two.
mkdir -p "The Test Tones/Night Signals"
cover /tmp/night.png 24 40 92 "NIGHT SIGNALS"
for i in 1 2 3 4; do
    f="The Test Tones/Night Signals/0$i Signal $i.mp3"
    titles=("" "Lighthouse" "Static Bloom" "Far Away Ping" "Last Transmission")
    if [ $i -le 2 ]; then
        ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 8 330 $((i+1))):s=44100:d=8,aformat=channel_layouts=stereo" \
            -i /tmp/night.png -map 0:a -map 1:v -c:a libmp3lame -b:a 128k -c:v png -id3v2_version 3 \
            -metadata title="${titles[$i]}" -metadata artist="The Test Tones" -metadata album="Night Signals" \
            -metadata album_artist="The Test Tones" -metadata track="$i/4" -disposition:v attached_pic "$f"
    else
        ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 8 330 $((i+1))):s=44100:d=8,aformat=channel_layouts=stereo" \
            -c:a libmp3lame -q:a 4 -id3v2_version 4 -metadata title="${titles[$i]}" -metadata artist="The Test Tones" \
            -metadata album="Night Signals" -metadata album_artist="The Test Tones" -metadata track="$i/4" "$f"
    fi
done

# 2. FLAC, 48 kHz, Vorbis comments, a cover.jpg in the folder.
mkdir -p "Ada Sine/Paper Boats"
cover "Ada Sine/Paper Boats/cover.jpg" 120 52 30 "PAPER BOATS"
n=1
for t in "Fold" "Float" "Sink Slowly"; do
    ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 7 392 $((n+2))):s=48000:d=7,aformat=channel_layouts=stereo" \
        -c:a flac -metadata TITLE="$t" -metadata ARTIST="Ada Sine" -metadata ALBUM="Paper Boats" \
        -metadata TRACKNUMBER="$n" "Ada Sine/Paper Boats/$n $t.flac"
    n=$((n + 1))
done

# 3. Ogg Vorbis, 32 kHz mono.
mkdir -p "Square Club/Ogg Mornings"
n=1
for t in "Toast" "Kettle Song"; do
    ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 6 262 $((n+4))):s=32000:d=6" -ac 1 \
        -c:a libvorbis -q:a 3 -metadata title="$t" -metadata artist="Square Club" -metadata album="Ogg Mornings" \
        -metadata tracknumber="$n" "Square Club/Ogg Mornings/$t.ogg"
    n=$((n + 1))
done

# 4. WAVs with no tags in a folder (the folder is the album): 8, 16, 24-bit, float, 22.05 kHz to 48 kHz.
mkdir -p "Field Recordings"
ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 5 220 3):s=22050:d=5" -ac 1 -c:a pcm_u8 "Field Recordings/creek.wav"
ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 5 440 5):s=48000:d=5,aformat=channel_layouts=stereo" -c:a pcm_s16le "Field Recordings/birds.wav"
ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 5 349 2):s=44100:d=5,aformat=channel_layouts=stereo" -c:a pcm_s24le "Field Recordings/wind.wav"
ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 5 294 6):s=96000:d=5,aformat=channel_layouts=stereo" -c:a pcm_f32le "Field Recordings/rain.wav"
# A tagged WAV (LIST INFO) with accents in it
ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 6 523 4):s=44100:d=6,aformat=channel_layouts=stereo" -c:a pcm_s16le \
    -metadata title="Café Déjà Vu" -metadata artist="Björk Sinewave" -metadata album="Über Tones" "Field Recordings/cafe.wav"

# 5. A compilation: one folder, an album name, different artists, no album artist.
mkdir -p "Mixtape"
n=1
for a in "Ada Sine" "Square Club" "Pulse Width"; do
    ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 5 466 $((n+1))):s=44100:d=5,aformat=channel_layouts=stereo" \
        -c:a libmp3lame -q:a 5 -id3v2_version 3 -metadata title="Track for $a" -metadata artist="$a" \
        -metadata album="Summer Mixtape" -metadata track="$n" "Mixtape/$n.mp3"
    n=$((n + 1))
done

# 6. Loose in the music folder: one song with no tags at all, and a broken one.
ffmpeg -loglevel error -f lavfi -i "aevalsrc=$(tune 4 587 3):s=44100:d=4" -ac 1 -c:a libmp3lame -q:a 6 -write_xing 0 "no tags at all.mp3"
head -c 20000 /dev/urandom > "broken.mp3"

find . -type f | sort
