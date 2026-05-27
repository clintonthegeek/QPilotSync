#!/usr/bin/env bash
#
# palm-wipe-pim.sh — wipe the PIM databases off a connecting Palm device so a
# sync test starts from a clean slate.
#
# Deletes (permanently) the Calendar/Datebook, Contacts/Address, ToDo and
# Memo databases via pilot-link's `pilot-xfer --delete`. The built-in PIM
# apps recreate these databases empty the next time they are opened, so the
# device is left factory-empty for those four apps — categories (AppInfo)
# included.
#
# CONNECTION — why not `usb:`
#   The distro pilot-link 0.13.0 segfaults when you use the `usb:` port: its
#   libusb-0.1 (libusb-compat) USB driver crashes inside USB_configure_device
#   on current kernels. We sidestep it entirely: the kernel `visor` usbserial
#   module exposes a connecting Palm as /dev/ttyUSB0 and /dev/ttyUSB1, and
#   pilot-link talks the HotSync protocol over that serial node without ever
#   touching libusb. Those nodes only exist for ~70s after you press HotSync,
#   so this script waits for the node to appear and then fires immediately.
#
#   Default: autodetect (prefers /dev/ttyUSB1, the usual Palm HotSync port,
#   falling back to /dev/ttyUSB0). If ttyUSB1 doesn't handshake on your unit,
#   force the other with `-p /dev/ttyUSB0`. (You must be in the `uucp` group
#   to open the node — `id` to check.)
#
# WildPalms syncs to the *classic* database names (DatebookDB / AddressDB /
# ToDoDB / MemoDB), so those are the default target set. Palm OS 5 devices
# whose *built-in* apps are the enhanced PIM (Calendar/Contacts/Tasks/Memos)
# store data under different names — pass --enhanced to also clear those.
#
# Usage:
#   scripts/palm-wipe-pim.sh [options]
#
# Options:
#   -p, --port <port>   Force a port instead of autodetecting. A /dev/tty*
#                       path is waited-for like the autodetect case; usb: and
#                       net:any are passed straight to pilot-xfer (usb: will
#                       crash — see above). Default: $PILOT_PORT or autodetect.
#   -w, --wait <secs>   How long to wait for the serial node after HotSync
#                       (default: 60).
#   -e, --enhanced      Also delete the Palm OS 5 enhanced PIM databases
#                       (CalendarDB-PDat, ContactsDB-PAdd, TasksDB-PTod,
#                       MemosDB-PMem). "Not found" lines for absent DBs are
#                       expected and harmless.
#   -l, --list          Don't delete — just list the databases on the device
#                       (handy to confirm the connection works before wiping).
#   -b, --backup <dir>  Back up the whole device to <dir> first, then wipe.
#                       Each needs its own HotSync press (separate connections).
#   -y, --yes           Skip the confirmation prompt (for scripted test loops).
#   -h, --help          Show this help.
#
# Examples:
#   scripts/palm-wipe-pim.sh -l              # verify the connection (no delete)
#   scripts/palm-wipe-pim.sh                 # wipe classic PIM, confirm first
#   scripts/palm-wipe-pim.sh -e -y           # wipe classic+enhanced, no prompt
#   scripts/palm-wipe-pim.sh -p /dev/ttyUSB0 # force the other serial node
#   scripts/palm-wipe-pim.sh -b ./palm-bak   # backup then wipe

set -euo pipefail

PORT="${PILOT_PORT:-}"      # empty => autodetect serial node
WAIT_SECS=60
ENHANCED=0
LIST_ONLY=0
ASSUME_YES=0
BACKUP_DIR=""

# Classic PIM databases — what WildPalms syncs to and what classic Palm apps use.
CLASSIC_DBS=(DatebookDB AddressDB ToDoDB MemoDB)
# Palm OS 5 enhanced PIM databases (built-in Calendar/Contacts/Tasks/Memos apps).
ENHANCED_DBS=(CalendarDB-PDat ContactsDB-PAdd TasksDB-PTod MemosDB-PMem)

# Serial nodes the kernel `visor` module creates, in preference order.
SERIAL_CANDIDATES=(/dev/ttyUSB1 /dev/ttyUSB0)

die() { echo "error: $*" >&2; exit 1; }

usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit "${1:-0}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--port)     PORT="${2:?--port needs a value}"; shift 2 ;;
        -w|--wait)     WAIT_SECS="${2:?--wait needs a value}"; shift 2 ;;
        -e|--enhanced) ENHANCED=1; shift ;;
        -l|--list)     LIST_ONLY=1; shift ;;
        -b|--backup)   BACKUP_DIR="${2:?--backup needs a directory}"; shift 2 ;;
        -y|--yes)      ASSUME_YES=1; shift ;;
        -h|--help)     usage 0 ;;
        *)             die "unknown argument: $1 (try --help)" ;;
    esac
done

command -v pilot-xfer >/dev/null 2>&1 || die "pilot-xfer not found — install pilot-link."

# Resolve the port to hand pilot-xfer, waiting for a serial node if needed.
# Sets the global RUNPORT. Prints the HotSync prompt as a side effect.
RUNPORT=""
resolve_port() {
    echo ">>> Put the Palm in its cradle/cable and press the HotSync button now."

    # Non-serial ports (usb:, net:any): pilot-xfer does its own listening.
    if [[ -n "$PORT" && "$PORT" != /dev/* ]]; then
        [[ "$PORT" == usb:* ]] && echo ">>> WARNING: 'usb:' is the driver that segfaults here." >&2
        RUNPORT="$PORT"
        return 0
    fi

    # Serial node: autodetect (preference order) or a specific forced /dev path.
    local candidates=("${SERIAL_CANDIDATES[@]}")
    [[ -n "$PORT" ]] && candidates=("$PORT")

    echo ">>> Waiting up to ${WAIT_SECS}s for the serial node (${candidates[*]})..."
    local waited=0
    while (( waited < WAIT_SECS )); do
        for dev in "${candidates[@]}"; do
            if [[ -e "$dev" ]]; then
                RUNPORT="$dev"
                echo ">>> Device appeared at $dev"
                return 0
            fi
        done
        sleep 1
        waited=$(( waited + 1 ))
    done
    die "no serial node appeared within ${WAIT_SECS}s — was HotSync pressed? Is the kernel 'visor' module bound? (check: journalctl -k | grep ttyUSB)"
}

run_pilot() {  # echoes exact argv, then runs it
    printf '+ '; printf '%q ' pilot-xfer "$@"; printf '\n'
    pilot-xfer "$@"
}

# --- list mode -------------------------------------------------------------
if [[ $LIST_ONLY -eq 1 ]]; then
    resolve_port
    run_pilot -p "$RUNPORT" -l
    exit 0
fi

# --- assemble target set ---------------------------------------------------
TARGETS=("${CLASSIC_DBS[@]}")
[[ $ENHANCED -eq 1 ]] && TARGETS+=("${ENHANCED_DBS[@]}")

echo "Port:     ${PORT:-autodetect (${SERIAL_CANDIDATES[*]})}"
echo "Will DELETE these databases (permanent):"
printf '  - %s\n' "${TARGETS[@]}"
[[ $ENHANCED -eq 1 ]] && echo "  (enhanced DBs absent on the device will report 'not found' — that's fine.)"
[[ -n "$BACKUP_DIR" ]] && echo "Backup first to: $BACKUP_DIR"

# --- confirm ---------------------------------------------------------------
if [[ $ASSUME_YES -eq 0 ]]; then
    read -r -p "Proceed? [y/N] " reply
    [[ "$reply" =~ ^[Yy]$ ]] || { echo "Aborted."; exit 1; }
fi

# --- optional backup (separate connection) ---------------------------------
if [[ -n "$BACKUP_DIR" ]]; then
    mkdir -p "$BACKUP_DIR"
    echo "=== Backup pass ==="
    resolve_port
    run_pilot -p "$RUNPORT" -b "$BACKUP_DIR"
    echo "Backup complete. The node drops after a sync — press HotSync again for the wipe."
fi

# --- wipe (single connection) ----------------------------------------------
echo "=== Wipe pass ==="
resolve_port
run_pilot -p "$RUNPORT" --delete "${TARGETS[@]}"

echo "Done. The built-in apps will recreate empty databases on next launch."
