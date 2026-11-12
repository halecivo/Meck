#!/bin/sh
# Discover the collections (creates the missing ones in Radicale), then sync
# every SYNC_EVERY seconds. Discover again once a day, so a calendar or an
# address book added in iCloud later shows up too.
set -u
export VDIRSYNCER_CONFIG=/config/config
EVERY="${SYNC_EVERY:-900}"
DISCOVER_EVERY="${DISCOVER_EVERY:-86400}"

discover() {
  # "Should vdirsyncer attempt to create it?" for every collection Radicale lacks: yes
  yes | vdirsyncer discover && vdirsyncer metasync
}

until discover; do
  echo "vdirsyncer: discover failed (password, network?), again in 5 min"
  sleep 300
done
last=$(date +%s)

while true; do
  now=$(date +%s)
  if [ $((now - last)) -ge "$DISCOVER_EVERY" ]; then
    discover && last=$now
  fi
  vdirsyncer sync || echo "vdirsyncer: sync failed, next try in $EVERY s"
  sleep "$EVERY"
done
