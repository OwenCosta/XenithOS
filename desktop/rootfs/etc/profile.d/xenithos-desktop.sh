# XenithOS Desktop's session environment (the first console's login shell reads this before it
# starts the session; xsession hands the same to programs started through D-Bus).
# - The desktop reads XenithOS's defaults (/etc/xdg/xenithos-desktop) before XFCE's own.
# - Its menus use XenithOS's names for its own apps (/usr/share/xenithos-desktop/overrides).
# - Apps made with Qt follow the desktop's look.
case ":${XDG_CONFIG_DIRS:-}:" in
    *:/etc/xdg/xenithos-desktop:*) ;;
    *) export XDG_CONFIG_DIRS="/etc/xdg/xenithos-desktop:${XDG_CONFIG_DIRS:-/etc/xdg}" ;;
esac
case ":${XDG_DATA_DIRS:-}:" in
    *:/usr/share/xenithos-desktop/overrides:*) ;;
    *) export XDG_DATA_DIRS="/usr/share/xenithos-desktop/overrides:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" ;;
esac
export QT_QPA_PLATFORMTHEME=gtk3
