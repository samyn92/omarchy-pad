#!/bin/bash
# Tablet layer for Omarchy on the OnePlus Pad 4. Run it on the tablet, as the owner, after the first login:
#
#   tablet/install-tablet-setup.sh [SCALE]       SCALE: 2 (default, 1696x1200 desktop) or 1.6 (2120x1500)
#
# Installs lisgd (touch gestures) from this repository's package (lisgd/PKGBUILD: 0.4.0 with a fix for miscounted
# fingers), wvkbd (on-screen keyboard) and, without that package, lisgd when the repositories have them, the three
# iceland-* helpers to /usr/local/bin, ~/.config/iceland/tablet.conf, and ~/.config/hypr/iceland_tablet.lua,
# which it loads from ~/.config/hypr/hyprland.lua.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
scale=${1:-2}
cfg=${XDG_CONFIG_HOME:-$HOME/.config}

[[ $scale =~ ^[0-9]+(\.[0-9]+)?$ ]] || { echo "SCALE must be a number such as 2 or 1.6" >&2; exit 2; }
command -v hyprctl >/dev/null || { echo "run this inside the Omarchy (Hyprland) session" >&2; exit 1; }

lisgd_pkg=$(compgen -G "$here/../lisgd/lisgd-*-$(uname -m).pkg.tar.*" | tail -n 1 || true)
[[ -z $lisgd_pkg ]] || sudo pacman -U --needed --noconfirm "$lisgd_pkg"

missing=()
for p in wvkbd lisgd; do
  if pacman -Q "$p" >/dev/null 2>&1; then
    continue
  elif pacman -Si "$p" >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm "$p"
  else
    missing+=("$p")
  fi
done

# lisgd reads the touch screen directly
id -nG "$USER" | grep -qw input || sudo usermod -aG input "$USER"

sudo install -m 0755 "$here"/bin/iceland-tablet-session "$here"/bin/iceland-osk "$here"/bin/iceland-rotate-lock \
  /usr/local/bin/

panel=$(hyprctl monitors -j | jq -r '[.[] | select(.name | test("^(DSI|eDP)"))][0].name // empty')
panel=${panel:-DSI-1}

mkdir -p "$cfg/iceland" "$cfg/hypr"
if [[ ! -f $cfg/iceland/tablet.conf ]]; then
  cat >"$cfg/iceland/tablet.conf" <<EOF
# OnePlus Pad 4 tablet session (iceland-tablet-session); log out and in after a change
PANEL=$panel
SCALE=$scale
# 1 = swap the two portrait directions, if the screen turns the wrong way
ROTATE_FLIP=0
# 0 = no lisgd touch gestures
GESTURES=1
EOF
fi
sed -e "s|@PANEL@|$panel|" -e "s|@SCALE@|$scale|" "$here/hypr/iceland_tablet.lua" >"$cfg/hypr/iceland_tablet.lua"

conf=$cfg/hypr/hyprland.lua
if [[ -f $conf ]] && ! grep -q 'require("hypr.iceland_tablet")' "$conf"; then
  if grep -q '^require("hypr.autostart")' "$conf"; then
    sed -i '/^require("hypr.autostart")/a require("hypr.iceland_tablet")' "$conf"
  else
    printf '\nrequire("hypr.iceland_tablet")\n' >>"$conf"
  fi
fi

hyprctl reload >/dev/null
echo "Tablet layer installed for $panel at scale $scale. Log out and in once (for the input group)."
if ((${#missing[@]})); then
  echo "Not in the repositories, build them from the AUR for the full setup: ${missing[*]}"
fi
