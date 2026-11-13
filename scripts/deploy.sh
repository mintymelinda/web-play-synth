#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

DATA_DIR="${SYNTH_DATA_DIR:-$ROOT_DIR/data}"
BUILDER_IMAGE="web-play-synth-builder:deploy"
NGINX_IMAGE="nginx:1.27-alpine"
RELEASE_ID="$(git rev-parse --short=12 HEAD 2>/dev/null || date -u +%Y%m%d%H%M%S)-$(date -u +%Y%m%d%H%M%S)"
RELEASES_DIR="$DATA_DIR/releases"
ASSETS_DIR="$DATA_DIR/assets"
STAGE_DIR="$RELEASES_DIR/.staging-$RELEASE_ID"
RELEASE_DIR="$RELEASES_DIR/$RELEASE_ID"
BUILDER_CONTAINER="web-play-synth-build-$RELEASE_ID"
PROBE_CONTAINER="web-play-synth-probe-$RELEASE_ID"
CURRENT_LINK="$DATA_DIR/current"
PREVIOUS_LINK="$DATA_DIR/previous"
TEMP_CURRENT="$DATA_DIR/.current-$RELEASE_ID"
TEMP_PREVIOUS="$DATA_DIR/.previous-$RELEASE_ID"

cleanup() {
    docker rm -f "$BUILDER_CONTAINER" "$PROBE_CONTAINER" >/dev/null 2>&1 || true
    rm -rf "$STAGE_DIR"
    rm -f "$TEMP_CURRENT" "$TEMP_PREVIOUS"
}
trap cleanup EXIT

mkdir -p "$RELEASES_DIR" "$ASSETS_DIR"

if [[ -e "$RELEASE_DIR" ]]; then
    echo "Release already exists: $RELEASE_DIR" >&2
    exit 1
fi

echo "Building static release $RELEASE_ID..."
docker build --target build --tag "$BUILDER_IMAGE" .
docker create --name "$BUILDER_CONTAINER" "$BUILDER_IMAGE" >/dev/null
mkdir -p "$STAGE_DIR"
docker cp "$BUILDER_CONTAINER:/app/dist/." "$STAGE_DIR/"

if [[ ! -s "$STAGE_DIR/index.html" ]]; then
    echo "Build did not produce a non-empty dist/index.html" >&2
    exit 1
fi

# Verify that every asset referenced directly by index.html exists before
# publishing anything. Vite emits content-hashed paths under /assets/.
while IFS= read -r asset; do
    [[ -z "$asset" ]] && continue
    if [[ ! -s "${STAGE_DIR}/${asset#/}" ]]; then
        echo "Build references a missing asset: $asset" >&2
        exit 1
    fi
done < <(grep -oE '/assets/[^"[:space:]]+' "$STAGE_DIR/index.html" | sort -u || true)

echo "Smoke-testing the staged release with Nginx..."
docker run --detach --rm --name "$PROBE_CONTAINER" \
    --volume "$STAGE_DIR:/usr/share/nginx/html:ro" \
    "$NGINX_IMAGE" >/dev/null
healthy=0
for attempt in {1..15}; do
    if docker exec "$PROBE_CONTAINER" wget -q -O /dev/null http://127.0.0.1/; then
        healthy=1
        break
    fi
    sleep 1
done
if [[ "$healthy" -ne 1 ]]; then
    echo "Staged release failed its Nginx HTTP smoke test" >&2
    exit 1
fi

# Publish immutable assets first. Never delete older hashed files: existing
# browser tabs may request them after the new index.html is live.
if [[ -d "$STAGE_DIR/assets" ]]; then
    cp -a "$STAGE_DIR/assets/." "$ASSETS_DIR/"
fi

mv "$STAGE_DIR" "$RELEASE_DIR"

# Keep the prior release for an instant rollback, then atomically replace the
# current symlink. Nginx remains running throughout the deployment.
if [[ -L "$CURRENT_LINK" ]]; then
    ln -s "$(readlink "$CURRENT_LINK")" "$TEMP_PREVIOUS"
    mv -Tf "$TEMP_PREVIOUS" "$PREVIOUS_LINK"
fi

ln -s "$RELEASE_DIR" "$TEMP_CURRENT"
mv -Tf "$TEMP_CURRENT" "$CURRENT_LINK"

echo "Activating release $RELEASE_ID..."
docker compose up -d --no-deps web-play-synth

echo "Deployment complete."
echo "Current release: $RELEASE_DIR"
if [[ -L "$PREVIOUS_LINK" ]]; then
    echo "Previous release: $(readlink "$PREVIOUS_LINK")"
fi
