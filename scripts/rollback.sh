#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Load persistent local deployment settings; explicit environment values win.
_cli_synth_data_dir="${SYNTH_DATA_DIR-}"
_cli_synth_port="${SYNTH_PORT-}"
if [[ -f "$ROOT_DIR/.env" ]]; then
    set -a
    # shellcheck disable=SC1091
    source "$ROOT_DIR/.env"
    set +a
fi
[[ -z "$_cli_synth_data_dir" ]] || SYNTH_DATA_DIR="$_cli_synth_data_dir"
[[ -z "$_cli_synth_port" ]] || SYNTH_PORT="$_cli_synth_port"
unset _cli_synth_data_dir _cli_synth_port

DATA_DIR="${SYNTH_DATA_DIR:-$ROOT_DIR/data}"
# Support both the Docker Compose plugin and Synology's standalone v1 CLI.
if docker compose version >/dev/null 2>&1; then
    COMPOSE=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE=(docker-compose)
else
    echo "Docker Compose is required (docker compose or docker-compose)." >&2
    exit 1
fi
CURRENT_LINK="$DATA_DIR/current"
PREVIOUS_LINK="$DATA_DIR/previous"
TEMP_CURRENT="$DATA_DIR/.rollback-current-$$"
TEMP_PREVIOUS="$DATA_DIR/.rollback-previous-$$"

cleanup() {
    rm -f "$TEMP_CURRENT" "$TEMP_PREVIOUS"
}
trap cleanup EXIT

if [[ ! -L "$PREVIOUS_LINK" ]]; then
    echo "No previous release is available to roll back to." >&2
    exit 1
fi

previous_target="$(readlink "$PREVIOUS_LINK")"
if [[ "$previous_target" != /* ]]; then
    previous_target="$DATA_DIR/$previous_target"
fi
if [[ ! -d "$previous_target" || ! -s "$previous_target/index.html" ]]; then
    echo "Previous release is missing or incomplete: $previous_target" >&2
    exit 1
fi

if [[ -L "$CURRENT_LINK" ]]; then
    ln -s "$(readlink "$CURRENT_LINK")" "$TEMP_PREVIOUS"
    mv -Tf "$TEMP_PREVIOUS" "$PREVIOUS_LINK"
fi

ln -s "releases/$(basename "$previous_target")" "$TEMP_CURRENT"
mv -Tf "$TEMP_CURRENT" "$CURRENT_LINK"

"${COMPOSE[@]}" up -d --no-deps web-play-synth
echo "Rolled back to $previous_target"
