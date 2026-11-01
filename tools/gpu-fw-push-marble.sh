#!/bin/sh
# Push the POCO F5 (marble) Adreno 662 "Adreno 725" firmware onto the phone's iOS
# root over SSH. The kernel's SweetAdreno loader reads a630_* names from
# /usr/local/share/firmware/ (and /usr/standalone/firmware/sweet/), so the A662
# images are delivered under the a630_* names it searches for: the bytes are the
# marble GPU's, only the filenames follow the loader.
#
#   tools/gpu-fw-push-marble.sh [FWDIR]
# FWDIR defaults to gpu/fw-marble (populated from the marble /vendor/firmware).
# Needs the phone booted with SSH up (root@192.168.7.2); SWEET_HOST overrides.
set -eu
host=${SWEET_HOST:-root@192.168.7.2}
here=$(cd "$(dirname "$0")" && pwd)
fwdir=${1:-$here/../gpu/fw-marble}
dest=/usr/local/share/firmware
ssh "$host" "mkdir -p $dest"
push() {
	[ -f "$fwdir/$1" ] || { echo "missing $fwdir/$1" >&2; return 1; }
	ssh "$host" "cat > $dest/$2 && chmod 644 $dest/$2" < "$fwdir/$1"
	echo "pushed $1 -> $2 ($(wc -c < "$fwdir/$1") bytes)"
}
# a630-named targets (the loader's search names) from the A662 source images
push a662_gmu.bin  a630_gmu.bin
push a660_sqe.fw   a630_sqe.fw
push a662_zap.mdt  a630_zap.mdt
push a662_zap.b00  a630_zap.b00
push a662_zap.b01  a630_zap.b01
push a662_zap.b02  a630_zap.b02
