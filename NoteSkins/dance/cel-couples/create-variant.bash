#!/bin/bash
set -eu
name="$1"
color="$2"
if [[ -z "$name" || -z "$color" ]]; then
	printf 'USAGE: %s NAME COLOR' "$0" >&2
	exit 1
fi

output="./variants/$name"
mkdir -p "$output"

tmp="$(mktemp -d)"
cleanup() {
	rm -rf -- "$tmp"
}
trap cleanup EXIT

echo "$output/metrics.ini"
cat > "$output/metrics.ini" << EOF
[Global]
FallbackNoteSkin=cel-couples

[NoteDisplay]
VariantColor=$color
EOF

parts=(
	'_Down Tails 1x8.png'
	'Down Hold Body Active.png'
	'Down Hold Body Inactive.png'
	'Down Hold BottomCap Active.png'
	'Down Hold BottomCap Inactive.png'
	'Down Roll Body Active.png'
	'Down Roll Body Inactive.png'
	'Down Roll BottomCap Active.png'
	'Down Roll BottomCap Inactive.png'
)
for part in "${parts[@]}"; do
	echo "$output/$part"
	magick "./$part" -colorspace sRGB \( +clone -fill "$color" -colorize 100 \) -compose multiply -composite "$output/$part"
done
