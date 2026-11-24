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
#   rt.run      present  -> keep rawtun running with the args in DIR/rt.args
#   rt.args     one line of rawtun arguments (read when rawtun is (re)started)
#   helper.pid  written here while this helper runs
# rawtun needs root for raw sockets, same as sing-box's utun, so it rides this
# same helper instead of asking for the password twice.
# The helper exits (and stops sing-box and rawtun) as soon as the app is gone.
#
# Usage: iamvpn-helper.sh <sing-box binary> <data dir> <app pid>

SB="$1"
DIR="$2"
APPPID="$3"
SBPID=""
RTPID=""
RT="$(dirname "$SB")/rawtun"

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

stop_rt() {
    if [ -n "$RTPID" ]; then
        kill "$RTPID" 2>/dev/null
        wait "$RTPID" 2>/dev/null
        RTPID=""
    fi
}

while kill -0 "$APPPID" 2>/dev/null; do
    # sing-box exited on its own (bad config, crash): drop the run flag so
    # the app sees it stopped instead of us restarting it in a loop.
    if [ -n "$SBPID" ] && ! kill -0 "$SBPID" 2>/dev/null; then
        SBPID=""
        rm -f "$DIR/sb.pid" "$DIR/sb.run"
    fi

    if [ -n "$RTPID" ] && ! kill -0 "$RTPID" 2>/dev/null; then
        RTPID=""   # rawtun died; relaunched below while rt.run is present
    fi

    # rawtun: start before sing-box (config points at its local port). Must be up
    # whenever rt.run is present; a new args file means a different server -> restart.
    if [ -f "$DIR/rt.run" ] && [ -x "$RT" ]; then
        if [ -z "$RTPID" ]; then
            # shellcheck disable=SC2086
            "$RT" $(cat "$DIR/rt.args" 2>/dev/null) >"$DIR/rawtun.log" 2>&1 &
            RTPID=$!
            chmod 666 "$DIR/rawtun.log" 2>/dev/null
        fi
    elif [ -n "$RTPID" ]; then
        stop_rt
    fi

    if [ -f "$DIR/rt.restart" ]; then
        rm -f "$DIR/rt.restart"
        stop_rt
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
stop_rt
rm -f "$DIR/helper.pid" "$DIR/sb.run" "$DIR/sb.restart" "$DIR/rt.run" "$DIR/rt.restart"
