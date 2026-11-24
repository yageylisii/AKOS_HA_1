#!/bin/sh
# Two Linux containers with separate network namespaces on a private Docker network.
set -eu
cd "$(dirname "$0")/.."
image=${HA4_IMAGE:-akos-ha4-test}
name="akos-ha4-$$"
mkdir -p logs/distributed
cleanup() {
    docker rm -f "$name-clients" "$name-server" >/dev/null 2>&1 || true
    docker network rm "$name" >/dev/null 2>&1 || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
docker network create "$name" >/dev/null
docker run -d --name "$name-server" --network "$name" "$image" \
    ./cc_server 4546 --config scripts/settings_small.txt --seed 12345 --delay 100 \
    --log /tmp/events.txt >/dev/null
docker run --rm --name "$name-clients" --network "$name" "$image" \
    python3 scripts/remote_clients.py "$name-server" 4546 --cabins 2 \
    >logs/distributed/clients.txt
code=$(docker wait "$name-server")
docker logs "$name-server" >logs/distributed/server.txt 2>&1
[ "$code" = 0 ]
cat logs/distributed/clients.txt
