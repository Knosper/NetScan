#!/usr/bin/env bash
# stop.sh — Graceful stop for the NetScan server.
# Usage: ./stop.sh
# Uses the PID file written by the server and sends SIGTERM.
# Falls back to pkill -9 only after a real timeout.

TIMEOUT_SECS=15
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="${SCRIPT_DIR}/netscan.pid"
if [ ! -f "${PID_FILE}" ]; then
    PID_FILE="${SCRIPT_DIR}/../netscan.pid"
fi

read_pid() {
    if [ ! -f "${PID_FILE}" ]; then
        return 1
    fi

    local pid
    pid="$(head -n 1 "${PID_FILE}" 2>/dev/null | tr -d '[:space:]')"
    if [[ ! "${pid}" =~ ^[0-9]+$ ]]; then
        return 1
    fi

    printf '%s' "${pid}"
}

PID="$(read_pid)"
if [ -z "${PID}" ]; then
    echo "PID file not found or invalid: ${PID_FILE}"
    echo "Falling back to process-name lookup."
else
    if [ -r "/proc/${PID}/cmdline" ]; then
        CMDLINE="$(tr '\0' ' ' < "/proc/${PID}/cmdline" 2>/dev/null)"
        if [[ "${CMDLINE}" != *netscan* ]]; then
            echo "PID file points to a non-NetScan process: ${PID}"
            unset PID
        fi
    fi
fi

if [ -n "${PID}" ]; then
    echo "Sending SIGTERM to PID ${PID} ..."

    echo "Shutdown request sent. Waiting up to ${TIMEOUT_SECS} seconds for process to exit ..."

    elapsed=0
    while [ $elapsed -lt $TIMEOUT_SECS ]; do
        kill -TERM "${PID}" > /dev/null 2>&1 || true
        sleep 1
        elapsed=$((elapsed + 1))
        if ! kill -0 "${PID}" > /dev/null 2>&1; then
            echo "Server stopped gracefully."
            exit 0
        fi
    done

    echo "Graceful shutdown timed out after ${TIMEOUT_SECS} seconds."
fi

# Last-resort hard kill after real timeout.
echo "Forcing process termination via pkill ..."
if pkill -9 -f netscan > /dev/null 2>&1; then
    echo "Process killed via pkill."
else
    echo "No netscan process found (already stopped or not running)."
fi

echo "Done."
