#!/usr/bin/env python3
"""Stage simulator for developing AskDeckScreen without any AI dependencies.

Speaks the exact protocol ask_deck.py speaks, so the C++ side (spawn,
non-blocking drain, partial-line buffering, stop-on-stdin, group kill, exit
detection) can be exercised on a laptop with no models, no mic, and no ollama.
Kept in the tree afterwards as a regression harness.

    python3 -u mock_deck.py --voice
    python3 -u mock_deck.py --text "what is a transistor"
    python3 -u mock_deck.py --voice --fail no_mic
    python3 -u mock_deck.py --pull qwen2.5:0.5b
    python3 -u mock_deck.py --selftest

--fail modes: no_mic no_speech no_ollama zim_error crash hang garbage
"""

import argparse
import json
import os
import random
import select
import signal
import subprocess
import sys
import time

ANSWER = (
    "A transistor is a small semiconductor device that switches or amplifies "
    "electrical signals. It has three terminals, and a small current or voltage "
    "at one of them controls a much larger current between the other two. "
    "Transistors are the basic building block of every modern computer chip."
)

_children = []


def emit(**kw):
    sys.stdout.write(json.dumps({k: str(v) for k, v in kw.items()}) + "\n")
    sys.stdout.flush()


def on_term(signum, frame):
    for p in _children:
        try:
            p.kill()
        except Exception:
            pass
    os._exit(1)


def wait_for_stdin(word, timeout):
    """Poll stdin for a control word. Returns True if it arrived."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        r, _, _ = select.select([sys.stdin], [], [], 0.1)
        if r:
            line = sys.stdin.readline()
            if not line:
                return False
            if line.strip() == word:
                return True
    return False


def fake_record(limit, fail):
    emit(stage="recording", limit=limit)
    if fail == "no_mic":
        time.sleep(0.4)
        emit(stage="error", code="no_mic",
             text="No capture device found (mock). Try the typed question path.")
        return False
    # Sit here until the UI sends "stop" or the cap expires, exactly like the
    # real recorder — this is what proves Enter-to-stop works end to end.
    wait_for_stdin("stop", limit)
    return True


def fake_speak(text, fail):
    emit(stage="speaking")
    # Interruptible, so the UI's "skip" path gets exercised too.
    wait_for_stdin("skip", min(4.0, 1.0 + len(text) / 90.0))


def run_query(question, fail, do_tts, slow):
    unit = 1.6 if slow else 0.5

    emit(stage="transcribing")
    time.sleep(unit)
    if fail == "no_speech":
        emit(stage="error", code="no_speech", text="Didn't catch that.")
        return 0
    if fail == "crash":
        raise RuntimeError("simulated helper crash")
    if fail == "garbage":
        # Non-JSON on stdout must land in the diagnostic ring, not break parsing.
        sys.stdout.write("this line is not json at all\n")
        sys.stdout.write('{"stage": "malformed"\n')
        sys.stdout.flush()
    emit(stage="transcript", text=question)

    emit(stage="searching")
    time.sleep(unit)
    if fail == "zim_error":
        emit(stage="context", used="0", reason="zim_error")
    elif "transistor" in question.lower() or "photosynthesis" in question.lower():
        emit(stage="context", used="1", title="Transistor", score="0.71")
    else:
        emit(stage="context", used="0", reason="low_score", score="0.12")

    emit(stage="thinking")
    if fail == "no_ollama":
        time.sleep(unit)
        emit(stage="error", code="no_ollama",
             text="Ollama is not installed. Run AI ASSETS setup.")
        return 0
    if fail == "hang":
        while True:  # only a group kill ends this
            time.sleep(1)
    time.sleep(unit * 3)
    emit(stage="answer", text=ANSWER)

    if do_tts:
        fake_speak(ANSWER, fail)
    emit(stage="done")
    return 0


def run_pull(tag, slow):
    emit(stage="log", text="mock pulling " + tag)
    for pct in range(0, 101, 4):
        emit(stage="pull", pct=pct)
        time.sleep(0.25 if slow else 0.06)
    emit(stage="done")
    return 0


def run_selftest():
    for name in ("whisper-bin", "whisper-model", "piper-bin", "piper-voice",
                 "ollama-bin", "ollama-model", "zim"):
        emit(stage="log", key=name, ok="1", text=name + " OK (mock)")
        time.sleep(0.05)
    emit(stage="done")
    return 0


def main():
    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)

    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", action="store_true")
    ap.add_argument("--text", default=None)
    ap.add_argument("--pull", default=None)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--max-seconds", type=float, default=30.0)
    ap.add_argument("--no-tts", action="store_true")
    ap.add_argument("--no-context", action="store_true")
    ap.add_argument("--fail", default=os.environ.get("CYBERDECK_MOCK_FAIL", ""))
    ap.add_argument("--slow", action="store_true")
    args = ap.parse_args()

    emit(stage="ready")

    try:
        if args.selftest:
            return run_selftest()
        if args.pull:
            return run_pull(args.pull, args.slow)
        if args.voice:
            if not fake_record(args.max_seconds, args.fail):
                return 1
            question = "what is a transistor"
        elif args.text:
            question = args.text
        else:
            emit(stage="error", code="bad_args", text="need --voice or --text")
            return 2
        return run_query(question, args.fail, not args.no_tts, args.slow)
    except Exception as exc:  # noqa: BLE001 - surface everything to the UI
        emit(stage="error", code="helper_exception", text=str(exc))
        return 1


if __name__ == "__main__":
    sys.exit(main())
