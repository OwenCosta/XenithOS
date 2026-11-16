#!/bin/bash
# Installs this XenithOS Desktop update: the packages next to this file. The updater runs it as
# root from the unpacked bundle, after the bundle's signature checked out (and get-update.sh
# does, the first time, on a PC installed before XenithOS Desktop could update itself).
#
# apt does the installing: it brings whatever else the new version needs (new apps come as
# things these packages depend on), and an install that's cut off can be picked up again by
# running this once more, which is what the updater does.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
export DEBIAN_FRONTEND=noninteractive
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
# Dpkg::Use-Pty=0: apt leaves the terminal alone. Run from Settings there may be one it isn't
# in front of, and touching it would stop the update for good.
OPTS=(-y --no-install-recommends -o DPkg::Lock::Timeout=300 -o Dpkg::Use-Pty=0 -o Dpkg::Options::=--force-confold -o Dpkg::Options::=--force-confdef)

# XenithOS Desktop 2 is built on Ubuntu 24.04, and these packages need Ubuntu's underneath. A PC
# with XenithOS Desktop 1 (built on Debian) can't be turned into it by an update.
if ! grep -qi '^Vendor: *Ubuntu' /etc/dpkg/origins/default 2>/dev/null; then
    echo "This update is XenithOS Desktop $(cat VERSION), which is built on Ubuntu. This PC's XenithOS Desktop is built on another system, so the update can't go on it. Install XenithOS Desktop $(cut -d. -f1 VERSION) from its ISO instead." >&2
    exit 1
fi

echo "== XenithOS Desktop $(cat VERSION): package sources"
# dpkg itself, so this works even when an earlier try left other packages half installed.
dpkg -i ./xenithos-archive_*.deb
apt-get update

# Questions two of the tools would ask while installing, answered: Wireshark may be used
# without being root (by people in its group), and macchanger doesn't change addresses by itself.
debconf-set-selections <<'EOF'
wireshark-common wireshark-common/install-setuid boolean true
macchanger macchanger/automatically_run boolean false
EOF

echo "== XenithOS Desktop $(cat VERSION): the desktop, its apps and tools"
# --fix-broken: also when an earlier try was cut off and left these packages half installed.
apt-get install --fix-broken "${OPTS[@]}" ./xenithos-archive_*.deb ./xenithos-desktop_*.deb ./xenithos-desktop-apps_*.deb ./xenithos-security-tools_*.deb
echo "== done"
