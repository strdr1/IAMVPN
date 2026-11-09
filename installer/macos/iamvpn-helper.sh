#!/bin/sh
# IAMVPN root helper for macOS.
#
# Started once per app session via `osascript ... with administrator
# privileges` (sing-box needs root to create the utun interface).
# It never decides anything itself: it follows flag files written by the
# (unprivileged) app in DIR:
#   sb.run      present  -> keep sing-box running with DIR/config.json
#   sb.restart  present  -> restart sing-box (new config), flag is consumed
#   sb.pid      written here while sing-box runs (app checks liveness)
#   helper.pid  written here while this helper runs
# The helper exits (and stops sing-box) as soon as the app process is gone.
#
# Usage: iamvpn-helper.sh <sing-box binary> <data dir> <app pid>

SB="$1"
DIR="$2"
APPPID="$3"
SBPID=""

cd "$DIR" || exit 1
echo $$ > "$DIR/helper.pid"

stop_sb() {
    if [ -n "$SBPID" ]; then
        kill "$SBPID" 2>/dev/null
        wait "$SBPID" 2>/dev/null
        SBPID=""
    fi
    rm -f "$DIR/sb.pid"
}

while kill -0 "$APPPID" 2>/dev/null; do
    # sing-box exited on its own (bad config, crash): drop the run flag so
    # the app sees it stopped instead of us restarting it in a loop.
    if [ -n "$SBPID" ] && ! kill -0 "$SBPID" 2>/dev/null; then
        SBPID=""
        rm -f "$DIR/sb.pid" "$DIR/sb.run"
    fi

    if [ -f "$DIR/sb.restart" ]; then
        rm -f "$DIR/sb.restart"
        stop_sb
    fi

    if [ -f "$DIR/sb.run" ]; then
        if [ -z "$SBPID" ]; then
            "$SB" run -c "$DIR/config.json" >"$DIR/sing-box.log" 2>"$DIR/sing-box.err" &
            SBPID=$!
            echo "$SBPID" > "$DIR/sb.pid"
            # Logs are created by root: let the app read and truncate them.
            chmod 666 "$DIR/sing-box.log" "$DIR/sing-box.err" 2>/dev/null
        fi
    elif [ -n "$SBPID" ]; then
        stop_sb
    fi

    sleep 0.3
done

stop_sb
rm -f "$DIR/helper.pid" "$DIR/sb.run" "$DIR/sb.restart"
