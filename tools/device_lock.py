#!/usr/bin/env python3
"""One T-Deck, several Claude Code sessions: a lock for the device.

Flashing or driving the deck over USB from two sessions at once breaks both
(one session's upload replaces the other's firmware mid-test, and COM ports
are exclusive). Sessions take this lock before touching the device and give
it back when done. The holder is identified by its git worktree, so each
session (each in its own worktree) is a separate holder.

  py -3 tools/device_lock.py status
  py -3 tools/device_lock.py acquire --task "power test on wifi-setup"
  py -3 tools/device_lock.py release
  py -3 tools/device_lock.py release --force      # stale lock of a dead session

Locks expire after --minutes (default 30) without use; every allowed device
command refreshes the holder's lock.

"hook" mode is the Claude Code PreToolUse hook (.claude/settings.json): it
reads the Bash tool call on stdin and, for a command that touches the
device (pio upload, esptool, serial tools, COM ports), takes the lock if it
is free or already ours, and blocks the command (exit 2) if another
session holds it.

The lock file lives outside the repositories: ~/.meck/device.lock (JSON).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

LOCK_DIR = os.path.join(os.path.expanduser("~"), ".meck")
LOCK_FILE = os.path.join(LOCK_DIR, "device.lock")
DEFAULT_MINUTES = 30

# Commands that reach the device
DEVICE_CMD = re.compile(
    r"(\bpio\b[^\n]*(-t|--target)\s*upload)"
    r"|esptool"
    r"|\bCOM\d+\b"
    r"|/dev/tty(USB|ACM)\d"
    r"|sd_upload\.py|press_keys\.py|grab_screen\.py",
    re.IGNORECASE,
)


def worktree_of(path):
    try:
        out = subprocess.run(["git", "-C", path, "rev-parse", "--show-toplevel"],
                             capture_output=True, text=True, timeout=10)
        if out.returncode == 0 and out.stdout.strip():
            return os.path.normcase(os.path.abspath(out.stdout.strip()))
    except (OSError, subprocess.SubprocessError):
        pass
    return os.path.normcase(os.path.abspath(path))


def read_lock():
    try:
        with open(LOCK_FILE, encoding="utf-8") as f:
            lock = json.load(f)
    except (OSError, ValueError):
        return None
    if lock.get("expires", 0) < time.time():
        return None   # expired: free
    return lock


def write_lock(owner, task, minutes):
    os.makedirs(LOCK_DIR, exist_ok=True)
    now = time.time()
    prev = read_lock()
    lock = {
        "owner": owner,
        "task": task or (prev or {}).get("task", ""),
        "since": (prev or {}).get("since", now) if prev and prev.get("owner") == owner else now,
        "expires": now + minutes * 60,
    }
    tmp = LOCK_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(lock, f)
    os.replace(tmp, LOCK_FILE)
    return lock


def describe(lock):
    age = (time.time() - lock["since"]) / 60
    left = (lock["expires"] - time.time()) / 60
    return f"{lock['owner']} ({lock.get('task') or 'no task given'}), held {age:.0f} min, expires in {left:.0f} min"


def cmd_status(_a):
    lock = read_lock()
    print(f"device locked by {describe(lock)}" if lock else "device free")
    return 0


def cmd_acquire(a):
    owner = a.owner or worktree_of(os.getcwd())
    lock = read_lock()
    if lock and lock["owner"] != owner:
        print(f"device busy: {describe(lock)}")
        return 1
    lock = write_lock(owner, a.task, a.minutes)
    print(f"device locked by {describe(lock)}")
    return 0


def cmd_release(a):
    owner = a.owner or worktree_of(os.getcwd())
    lock = read_lock()
    if not lock:
        print("device already free")
        return 0
    if lock["owner"] != owner and not a.force:
        print(f"not yours: {describe(lock)} (use --force only for a dead session)")
        return 1
    try:
        os.remove(LOCK_FILE)
    except OSError:
        pass
    print("device free")
    return 0


def cmd_hook(_a):
    try:
        data = json.load(sys.stdin)
    except ValueError:
        return 0
    command = (data.get("tool_input") or {}).get("command", "")
    if not command or not DEVICE_CMD.search(command):
        return 0
    if re.search(r"device_lock\.py", command):
        return 0   # the lock tool itself
    owner = worktree_of(data.get("cwd") or os.getcwd())
    lock = read_lock()
    if lock and lock["owner"] != owner:
        sys.stderr.write(
            "The T-Deck is in use by another session: " + describe(lock) + ".\n"
            "Wait until it is released (py -3 tools/device_lock.py status), or ask the user. "
            "Do not touch the device meanwhile.\n")
        return 2
    write_lock(owner, (lock or {}).get("task") or "auto: " + command.strip().splitlines()[0][:80], DEFAULT_MINUTES)
    return 0


def main():
    ap = argparse.ArgumentParser(description="Lock for the T-Deck shared by Claude Code sessions")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    p = sub.add_parser("acquire")
    p.add_argument("--task", default="")
    p.add_argument("--minutes", type=int, default=DEFAULT_MINUTES)
    p.add_argument("--owner", default=None)
    p = sub.add_parser("release")
    p.add_argument("--force", action="store_true")
    p.add_argument("--owner", default=None)
    sub.add_parser("hook")
    a = ap.parse_args()
    return {"status": cmd_status, "acquire": cmd_acquire, "release": cmd_release, "hook": cmd_hook}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
