#!/usr/bin/env bash
# =============================================================================
#  convert_video.sh — Convert a video file for ESP32 playback
#
#  Usage:
#    ./convert_video.sh input.mp4 [output.avi] [fps] [quality]
#
#  Defaults:
#    output  = input filename with .avi extension in current directory
#    fps     = 15  (frames per second; 15 is safest, 24-30 possible on ESP32S3)
#    quality = 5   (JPEG quality; 1=best/largest, 31=worst/smallest; 4-6 recommended)
#
#  Requires: ffmpeg (https://ffmpeg.org/)
#
#  Output format:
#    • Video : MJPEG, 240×240 (cropped/scaled to fit round display)
#    • Audio : PCM 16-bit signed, mono, 22050 Hz (small and fast)
#    • Container : AVI (RIFF)
#
#  Copy the resulting .avi file to the root of your FAT32 SD card.
# =============================================================================

set -e

INPUT="$1"
OUTPUT="${2:-${INPUT%.*}.avi}"
FPS="${3:-15}"
QUALITY="${4:-5}"

if [ -z "$INPUT" ]; then
    echo "Usage: $0 input.mp4 [output.avi] [fps] [quality]"
    echo "  fps      : target frame rate (default 15)"
    echo "  quality  : JPEG quality 1-31, lower=better (default 5)"
    exit 1
fi

if ! command -v ffmpeg &>/dev/null; then
    echo "Error: ffmpeg not found. Install from https://ffmpeg.org/"
    exit 1
fi

echo "================================================"
echo "  Input   : $INPUT"
echo "  Output  : $OUTPUT"
echo "  FPS     : $FPS"
echo "  Quality : $QUALITY"
echo "================================================"

# ---------------------------------------------------------------------------
# Scale and crop to exactly 240×240.
# The filter chain:
#   1. Scale shortest side to 240, preserving aspect ratio
#   2. Crop centre 240×240
# ---------------------------------------------------------------------------
VFILTER="scale=240:240:force_original_aspect_ratio=increase,crop=240:240"

ffmpeg -y \
    -i "$INPUT" \
    -vf "$VFILTER" \
    -r "$FPS" \
    -vcodec mjpeg \
    -q:v "$QUALITY" \
    -pix_fmt yuvj420p \
    -huffman 0 \
    -acodec pcm_s16le \
    -ar 22050 \
    -ac 1 \
    "$OUTPUT"

# ---------------------------------------------------------------------------
# Report output file size
# ---------------------------------------------------------------------------
if [ -f "$OUTPUT" ]; then
    SIZE=$(du -sh "$OUTPUT" | cut -f1)
    DURATION=$(ffprobe -v quiet -show_entries format=duration -of csv=p=0 "$OUTPUT" 2>/dev/null || echo "?")
    echo ""
    echo "Done!"
    echo "  File     : $OUTPUT  ($SIZE)"
    echo "  Duration : ${DURATION}s"
    echo ""
    echo "Copy $OUTPUT to the root of your SD card and power on the player."
else
    echo "Conversion failed — check ffmpeg output above."
    exit 1
fi
