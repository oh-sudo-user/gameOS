#!/bin/sh
# Verify the build output before publishing an image:
#   scripts/verify-image.sh <images-dir> [target-dir]
# <images-dir> is buildroot/output/images, [target-dir] is output/target.
# The target checks are optional so the script also works on a bare image set.
set -u

IMAGES=${1:-}
TARGET=${2:-${GAMEOS_TARGET:-}}
fail=0

note_fail() {
    echo "FAIL: $1"
    fail=1
}

if [ -z "$IMAGES" ] || [ ! -d "$IMAGES" ]; then
    echo "usage: $0 <images-dir> [target-dir]"
    exit 2
fi

echo "== required artifacts"
for f in Image boot.vfat gameOS-boot.tar.gz gameOS-rgds-plus.img.gz; do
    if [ -s "$IMAGES/$f" ]; then
        echo "ok   $f"
    else
        note_fail "missing or empty $f"
    fi
done

echo "== sha256"
if ls "$IMAGES"/*.sha256 >/dev/null 2>&1; then
    if (cd "$IMAGES" && sha256sum -c --quiet ./*.sha256); then
        echo "ok   sha256 manifest"
    else
        note_fail "sha256 mismatch"
    fi
else
    note_fail "no .sha256 manifest"
fi

echo "== gzip integrity"
if [ -f "$IMAGES/gameOS-rgds-plus.img.gz" ]; then
    if gzip -t "$IMAGES/gameOS-rgds-plus.img.gz"; then
        echo "ok   gzip stream"
    else
        note_fail "corrupt gzip image"
    fi
fi

echo "== boot payload"
if [ -f "$IMAGES/gameOS-boot.tar.gz" ]; then
    list=$(tar -tzf "$IMAGES/gameOS-boot.tar.gz" 2>/dev/null) || list=
    for f in Image boot.scr.rgds-plus boot.scr.rgds; do
        case "$(printf '%s\n' "$list")" in
            *"$f"*) echo "ok   $f" ;;
            *) note_fail "boot payload missing $f" ;;
        esac
    done
fi

if [ -n "$TARGET" ]; then
    if [ ! -d "$TARGET" ]; then
        note_fail "target dir not found: $TARGET"
    else
        echo "== target payload"
        for f in usr/bin/gameos-ui usr/bin/gameos-splash usr/bin/gameos-update \
                 usr/bin/gameos-bt usr/bin/gameos-wifi usr/bin/retroarch \
                 etc/gameos/ota-pub.pem \
                 usr/share/gameos/fonts/NotoSansSC-Regular.otf; do
            if [ -s "$TARGET/$f" ]; then
                echo "ok   $f"
            else
                note_fail "target missing $f"
            fi
        done
        if [ -n "$(find "$TARGET/usr/bin" -maxdepth 1 -type f -perm -0002 2>/dev/null)" ]; then
            note_fail "world-writable file in /usr/bin"
        fi
    fi
fi

# The palettes live in the rootfs overlay, which Buildroot applies when the
# rootfs is assembled; the target tree alone can be stale, so prefer the image.
if [ -f "$IMAGES/rootfs.ext4" ] && command -v debugfs >/dev/null 2>&1; then
    echo "== rootfs themes"
    for theme in neon pixel fresh ink; do
        out=$(debugfs -R "cat /usr/share/gameos/assets/themes/$theme/palette.conf" \
              "$IMAGES/rootfs.ext4" 2>/dev/null) || out=
        case "$out" in
            ""|*"File not found"*) note_fail "theme $theme missing from rootfs" ;;
            *) echo "ok   theme $theme" ;;
        esac
    done
elif [ -n "$TARGET" ] && [ -d "$TARGET" ]; then
    echo "== rootfs themes (target tree; rebuild rootfs to verify the image)"
    for theme in neon pixel fresh ink; do
        if [ -f "$TARGET/usr/share/gameos/assets/themes/$theme/palette.conf" ]; then
            echo "ok   theme $theme"
        else
            note_fail "theme $theme missing"
        fi
    done
fi

if [ "$fail" -ne 0 ]; then
    echo "IMAGE: FAILED"
    exit 1
fi
echo "IMAGE: PASSED"
