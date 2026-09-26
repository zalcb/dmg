set -euo pipefail

analyzer=$(command -v "${SENTRUX:-sentrux}")
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME="$work/home"
mkdir -p "$HOME" "$work/baseline" "$work/current"
"$analyzer" --version
mv "$HOME/.sentrux/plugins/c" "$HOME/.sentrux/plugins/pinned-c"
mv "$HOME/.sentrux/plugins/objective-c" "$HOME/.sentrux/plugins/pinned-objective-c"
export SENTRUX_SKIP_GRAMMAR_DOWNLOAD=1

for profile in historical c; do
    python3 - "$profile" <<'PY'
import pathlib
import re
import sys

extensions = {"c": '["c"]', "objective-c": '["m", "h"]'}
if sys.argv[1] == "c":
    extensions = {"c": '["c", "h"]', "objective-c": '["m"]'}
for name, value in extensions.items():
    path = pathlib.Path.home() / ".sentrux/plugins" / f"pinned-{name}" / "plugin.toml"
    text, count = re.subn(r'^extensions = .*$', f"extensions = {value}", path.read_text(), flags=re.M)
    assert count == 1, path
    path.write_text(text)
PY
    if [ "$profile" = historical ]; then
        "$analyzer" check "$root" | tee "$work/historical.txt"
        "$analyzer" gate "$root"
        test "$(awk '/^Quality:/ {print $2}' "$work/historical.txt")" -ge 8034
    else
        git -C "$root" archive a85c4c4 | tar -x -C "$work/baseline"
        git -C "$root" checkout-index --all --prefix="$work/current/"
        "$analyzer" check "$work/baseline" | tee "$work/baseline.txt"
        "$analyzer" gate --save "$work/baseline"
        cp "$work/baseline/.sentrux/baseline.json" "$work/current/.sentrux/baseline.json"
        "$analyzer" check "$work/current" | tee "$work/current.txt"
        "$analyzer" gate "$work/current"
        test "$(awk '/^Quality:/ {print $2}' "$work/current.txt")" -ge \
             "$(awk '/^Quality:/ {print $2}' "$work/baseline.txt")"
    fi
done
