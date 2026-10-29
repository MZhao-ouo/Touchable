#!/bin/bash
set -euo pipefail
PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_ROOT"
LOCAL_ROOT="$PROJECT_ROOT/../.touchable"
DEPENDENCIES="$LOCAL_ROOT/dependencies"
mkdir -p "$DEPENDENCIES" "$LOCAL_ROOT/build/tooling/logs"
fetch_source() {
    local url=$1 path=$2 revision=$3
    if [ ! -d "$path/.git" ]; then
        mkdir -p "$path"
        git -C "$path" init -q
        git -C "$path" remote add origin "$url"
    fi
    if ! git -C "$path" rev-parse --verify HEAD >/dev/null 2>&1; then
        git -C "$path" fetch --depth 1 origin "$revision"
        git -C "$path" checkout --detach FETCH_HEAD
    fi
    if [ "$(git -C "$path" rev-parse HEAD)" != "$revision" ]; then
        echo "Unexpected source revision: $path" >&2
        exit 1
    fi
}
fetch_source https://github.com/jhaizhou-ops/d2r-mobile.git "$DEPENDENCIES/d2r-mobile" 2eb5758771f110cd53082b999d88d242766610da
fetch_source https://github.com/SciresM/hactool.git "$DEPENDENCIES/hactool" 1d64a83450e025622f3468c28fc4164dad2c5ef6
if git -C "$DEPENDENCIES/d2r-mobile" apply --check "$PROJECT_ROOT/patches/d2r-mobile.patch" 2>/dev/null; then
    # Mark the new files so that git diff shows them. The launcher compares that diff with the patch.
    git -C "$DEPENDENCIES/d2r-mobile" apply --intent-to-add "$PROJECT_ROOT/patches/d2r-mobile.patch"
else
    git -C "$DEPENDENCIES/d2r-mobile" apply --reverse --check "$PROJECT_ROOT/patches/d2r-mobile.patch"
fi
if git -C "$DEPENDENCIES/hactool" apply --check "$PROJECT_ROOT/patches/hactool.patch" 2>/dev/null; then
    git -C "$DEPENDENCIES/hactool" apply "$PROJECT_ROOT/patches/hactool.patch"
else
    git -C "$DEPENDENCIES/hactool" apply --reverse --check "$PROJECT_ROOT/patches/hactool.patch"
fi
if [ ! -f "$DEPENDENCIES/hactool/config.mk" ]; then
    cp "$DEPENDENCIES/hactool/config.mk.template" "$DEPENDENCIES/hactool/config.mk"
fi
make -C "$DEPENDENCIES/hactool" -j4 > "$LOCAL_ROOT/build/tooling/logs/hactool-build.log" 2>&1
if [ ! -x "$LOCAL_ROOT/venv/bin/python" ]; then
    xcrun python3 -m venv "$LOCAL_ROOT/venv"
fi
"$LOCAL_ROOT/venv/bin/python" -m pip install -r requirements.txt
shasum -a 256 requirements.txt | awk '{print $1}' > "$LOCAL_ROOT/venv/.touchable-ready"
echo "Local preparation tools are ready. This does not build the iOS game."
