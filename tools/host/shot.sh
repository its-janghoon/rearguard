#!/bin/bash
# Take a screenshot on the guest and save it here as PNG.  usage: shot.sh name
N=${1:-shot}
D=$HOME/qnx-setup/shots
mkdir -p "$D"
python3 "$HOME/qnx-setup/con.py" "sudo -n screenshot -file=/tmp/$N.bmp >/dev/null 2>&1; gzip -1 -c /tmp/$N.bmp | base64" 300 > "$D/$N.b64" || exit 1
base64 -d -i "$D/$N.b64" | gunzip > "$D/$N.bmp" && magick "$D/$N.bmp" "$D/$N.png" && rm -f "$D/$N.b64" "$D/$N.bmp" && echo "$D/$N.png"
