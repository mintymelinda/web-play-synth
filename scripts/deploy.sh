#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Keep this wrapper aligned with the normal NAS update command:
# build the application image and replace the running container with it.
if docker compose version >/dev/null 2>&1; then
    docker compose up -d --build --force-recreate
elif command -v docker-compose >/dev/null 2>&1; then
    docker-compose up -d --build --force-recreate
else
    echo "Docker Compose is required (docker compose or docker-compose)." >&2
    exit 1
fi
