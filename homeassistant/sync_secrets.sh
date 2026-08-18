#!/bin/sh
# Write gitignored homeassistant/secrets.yaml from this repo's secrets.env.
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
if [ ! -f "$ROOT/secrets.env" ]; then
    echo "missing $ROOT/secrets.env" >&2
    exit 1
fi
set -a
# shellcheck disable=SC1091
. "$ROOT/secrets.env"
set +a
if [ -z "${WAKETYPE_DEVICE_KEY:-}" ]; then
    echo "WAKETYPE_DEVICE_KEY is empty" >&2
    exit 1
fi
umask 077
printf 'waketype_bearer: "Bearer %s"\n' "$WAKETYPE_DEVICE_KEY" > "$ROOT/homeassistant/secrets.yaml"
echo "wrote homeassistant/secrets.yaml (gitignored)"
