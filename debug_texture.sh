#!/bin/bash
# Convert raw RGBA texture dumps to PNG for visual inspection
# Usage: ./debug_texture.sh [width] [height]
# Defaults to 1024x1024 if not specified

W=${1:-1024}
H=${2:-1024}

echo "Converting debug textures (${W}x${H})..."

for raw in /tmp/wowee_composite_debug.raw /tmp/wowee_equip_composite_debug.raw; do
    if [ -f "$raw" ]; then
        png="${raw%.raw}.png"
        # Try ImageMagick first, fall back to ffmpeg
        if command -v convert &>/dev/null; then
            convert -size ${W}x${H} -depth 8 rgba:"$raw" "$png" 2>/dev/null && \
                echo "Created $png (${W}x${H})" || \
                echo "Failed to convert $raw"
        elif command -v ffmpeg &>/dev/null; then
            ffmpeg -y -f rawvideo -pix_fmt rgba -s ${W}x${H} -i "$raw" "$png" 2>/dev/null && \
                echo "Created $png (${W}x${H})" || \
                echo "Failed to convert $raw"
        else
            echo "Need 'convert' (ImageMagick) or 'ffmpeg' to convert $raw"
            echo "  Install: sudo apt install imagemagick"
        fi
    else
        echo "Not found: $raw"
    fi
done
