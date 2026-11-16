#!/bin/bash
# Tries the XenithOS Desktop ISO in a VirtualBox VM: makes the VM (4 GB of memory, a 32 GB
# drive), puts the ISO in its DVD drive and starts it. Runs in Git Bash on Windows, and on
# Linux or macOS.
#
#   bash desktop/test-in-virtualbox.sh [options]
#
# Options
#   --iso FILE       the ISO (default: the newest in desktop/out)
#   --bios           start like an old PC (BIOS) instead of UEFI
#   --secure-boot    UEFI with Secure Boot on (Microsoft's keys, like a real PC)
#   --headless       no window (take pictures with: VBoxManage controlvm NAME screenshotpng FILE)
#   --name NAME      the VM's name (default "XenithOS Desktop test")
#   --eject          take the ISO out (after installing), then start the VM from its drive
#   --delete         delete the VM and its drive
# The VM's files go in desktop/out/vm/.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OUT="$HERE/out"
NAME="XenithOS Desktop test"
ISO=""
FIRMWARE=efi
SECURE=0
TYPE=gui
ACTION=create

while [ $# -gt 0 ]; do
    case "$1" in
        --iso) ISO="${2:?}"; shift ;;
        --bios) FIRMWARE=bios ;;
        --secure-boot) SECURE=1 ;;
        --headless) TYPE=headless ;;
        --name) NAME="${2:?}"; shift ;;
        --eject) ACTION=eject ;;
        --delete) ACTION=delete ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | grep '^#' | cut -c3-; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

VBM=VBoxManage
WINPATHS=0
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        WINPATHS=1
        command -v VBoxManage >/dev/null || VBM="/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"
        ;;
    Darwin) command -v VBoxManage >/dev/null || VBM=/Applications/VirtualBox.app/Contents/MacOS/VBoxManage ;;
esac
[ -x "$VBM" ] || command -v "$VBM" >/dev/null || { echo "VirtualBox isn't installed" >&2; exit 1; }
host_path() { if [ "$WINPATHS" = 1 ]; then cygpath -w "$1"; else echo "$1"; fi; }
vbm() { "$VBM" "$@"; }

VM_DIR="$OUT/vm"
DISK="$VM_DIR/$NAME.vdi"

if [ "$ACTION" = delete ]; then
    vbm controlvm "$NAME" poweroff >/dev/null 2>&1 || true
    sleep 2
    vbm unregistervm "$NAME" --delete >/dev/null 2>&1 || true
    vbm closemedium disk "$(host_path "$DISK")" --delete >/dev/null 2>&1 || true
    echo "Deleted the VM \"$NAME\""
    exit 0
fi

if [ "$ACTION" = eject ]; then
    vbm controlvm "$NAME" poweroff >/dev/null 2>&1 || true
    sleep 3
    vbm storageattach "$NAME" --storagectl SATA --port 1 --device 0 --type dvddrive --medium emptydrive
    vbm startvm "$NAME" --type "$TYPE"
    exit 0
fi

if [ -z "$ISO" ]; then
    ISO=$(ls -t "$OUT"/*.iso 2>/dev/null | head -n 1 || true)
fi
[ -f "$ISO" ] || { echo "No ISO: build one with desktop/build-in-docker.sh, or pass --iso" >&2; exit 1; }

if vbm showvminfo "$NAME" >/dev/null 2>&1; then
    echo "There's already a VM called \"$NAME\". Delete it first: $0 --delete" >&2
    exit 1
fi
mkdir -p "$VM_DIR"
vbm createvm --name "$NAME" --ostype Ubuntu_64 --register --basefolder "$(host_path "$VM_DIR")" >/dev/null
vbm modifyvm "$NAME" --memory 4096 --cpus 4 --firmware "$FIRMWARE" --graphicscontroller vmsvga --vram 128 \
    --nic1 nat --mouse usbtablet --usbohci on --boot1 dvd --boot2 disk --boot3 none --boot4 none --rtc-use-utc on
if [ "$SECURE" = 1 ]; then
    [ "$FIRMWARE" = efi ] || { echo "Secure Boot needs UEFI" >&2; exit 1; }
    vbm modifynvram "$NAME" inituefivarstore
    vbm modifynvram "$NAME" enrollmssignatures
    vbm modifynvram "$NAME" enrollorclpk
    vbm modifynvram "$NAME" secureboot --enable
fi
vbm createmedium disk --filename "$(host_path "$DISK")" --size 32768 --format VDI >/dev/null
vbm storagectl "$NAME" --name SATA --add sata --controller IntelAhci --portcount 2
vbm storageattach "$NAME" --storagectl SATA --port 0 --device 0 --type hdd --medium "$(host_path "$DISK")"
vbm storageattach "$NAME" --storagectl SATA --port 1 --device 0 --type dvddrive --medium "$(host_path "$ISO")"
vbm startvm "$NAME" --type "$TYPE"
echo "Started \"$NAME\" ($FIRMWARE$([ "$SECURE" = 1 ] && echo ', Secure Boot')) from $(basename "$ISO")."
echo "After installing: $0 --name \"$NAME\" --eject   (starts it from its drive)"
