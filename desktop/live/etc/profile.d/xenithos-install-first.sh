# XenithOS Desktop's live system (the USB stick): started from the boot menu's "Install
# XenithOS Desktop", the installer opens before XenithOS does. The installer leaves this out of
# installed systems.
if [ -z "$DISPLAY" ] && [ "$(tty)" = /dev/tty1 ] && [ -f "$HOME/.config/xenithos/install-first" ]; then
    rm -f "$HOME/.config/xenithos/install-first"
    startx /usr/lib/xenithos-desktop/installer-session -- -keeptty > "$HOME/.config/xenithos/installer.log" 2>&1
fi
