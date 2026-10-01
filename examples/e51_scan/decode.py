#!/usr/bin/env python3
"""Read the e51_scan results out of a running HT5023 over SWD and decode them.

    python3 examples/e51_scan/decode.py              # read the target, then decode
    python3 examples/e51_scan/decode.py --dump f.bin # decode a saved dump

Reading uses J-Link Commander's `savebin` on RAM only: the target is neither
halted nor reset, and flash is never touched.
"""

import argparse
import bisect
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EDGE_REC = 0x7FFF
STANDARD_BAUDS = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]
METHODS = {0: "UART5 hw TX", 1: "bit-banged TX", 2: "listen only"}
HEADER = struct.Struct("<8I")
TRIAL_HDR = struct.Struct("<4B5I")
PROBE = struct.Struct("<4BI4B")
LOOP = struct.Struct("<I4H4B40s")


def symbol(elf, name):
    out = subprocess.run(["arm-none-eabi-nm", "-S", str(elf)], capture_output=True, text=True).stdout
    m = re.search(r"^([0-9a-f]{8}) ([0-9a-f]{8}) [BbDd] %s$" % re.escape(name), out, re.M)
    if not m:
        sys.exit(f"{name} not found in {elf} -- is build/app.elf the e51_scan build?")
    return int(m.group(1), 16), int(m.group(2), 16)


def read_target(addr, size):
    tmp = Path(tempfile.mkdtemp())
    dump, script = tmp / "scan.bin", tmp / "read.jlink"
    script.write_text(f"savebin {dump}, 0x{addr:08X}, 0x{size:X}\nq\n")
    out = subprocess.run(["JLinkExe", "-device", "Cortex-M0", "-if", "SWD", "-speed", "1000",
                          "-autoconnect", "1", "-CommanderScript", str(script)],
                         capture_output=True, text=True, errors="replace").stdout
    if "into file...O.K." not in out:
        sys.exit("J-Link read failed:\n" + out[-1500:])
    return dump.read_bytes()


def parse(data):
    magic, fcpu, phase, trial, skipped, listen_ms, n_trials, cap = HEADER.unpack_from(data, 0)
    size = TRIAL_HDR.size + 2 * cap
    size += (-size) % 4
    trials = []
    for i in range(n_trials + 2):
        off = HEADER.size + i * size
        tx, rx, method, start, baud, edges, n, loop_x16, _prev = TRIAL_HDR.unpack_from(data, off)
        recs = struct.unpack_from(f"<{min(n, cap)}H", data, off + TRIAL_HDR.size)
        trials.append(dict(tx=tx, rx=rx, method=method, start=start, baud=baud, edges=edges,
                           n=n, loop=loop_x16 / 16.0, recs=recs))
    probes = []
    off = HEADER.size + (n_trials + 2) * size
    for i in range(4):
        pin, drive, first, final, change, pulled, *_ = PROBE.unpack_from(data, off + i * PROBE.size)
        probes.append(dict(pin=pin, drive=drive, first=first, final=final, change=change, pulled=pulled))
    off += 4 * PROBE.size
    counts = struct.unpack_from("<96H", data, off)
    sniff_port, sniff_pin = data[off + 192], data[off + 193]
    tx, rx, method, start, baud, edges, n, loop_x16, _prev = TRIAL_HDR.unpack_from(data, off + 196)
    recs = struct.unpack_from(f"<{min(n, cap)}H", data, off + 196 + TRIAL_HDR.size)
    sniff = dict(tx=tx, rx=rx, method=method, start=start, baud=baud, edges=edges, n=n,
                 loop=loop_x16 / 16.0, recs=recs)
    off += 196 + size
    listen_counts = struct.unpack_from("<96H", data, off)
    hit_port, hit_pin = data[off + 192], data[off + 193]
    tx, rx, method, start, baud, edges, n, loop_x16, _prev = TRIAL_HDR.unpack_from(data, off + 196)
    recs = struct.unpack_from(f"<{min(n, cap)}H", data, off + 196 + TRIAL_HDR.size)
    hit = dict(tx=tx, rx=rx, method=method, start=start, baud=0, edges=edges, n=n,
               loop=loop_x16 / 16.0, recs=recs)
    off += 196 + size
    loops = []
    for i in range(5):
        baud, sent, received, matched, bad_i, bad_s, bad_g, tlen, uart, text = LOOP.unpack_from(data, off + i * LOOP.size)
        loops.append(dict(baud=baud, sent=sent, received=received, matched=matched, bad_i=bad_i,
                          bad_s=bad_s, bad_g=bad_g, text=text[:tlen], uart=uart))
    return dict(magic=magic, fcpu=fcpu, phase=phase, trial=trial, skipped=skipped,
                listen_ms=listen_ms, n_trials=n_trials, trials=trials, probes=probes,
                sniff_counts=counts, sniff_port=sniff_port, sniff_pin=sniff_pin, sniff=sniff,
                listen_counts=listen_counts, hit_port=hit_port, hit_pin=hit_pin, hit=hit, loops=loops)


def transitions(t):
    """(times, levels) of real edges; a saturated gap counts as a long idle."""
    now, cur, times, levels = 0, t["start"], [], []
    for r in t["recs"]:
        d, lvl = r & EDGE_REC, r >> 15
        now += d if d < EDGE_REC else 10 ** 8
        if lvl != cur:
            times.append(now)
            levels.append(lvl)
            cur = lvl
    return times, levels


def uart_decode(t, times, levels, period):
    def level_at(x):
        i = bisect.bisect_right(times, x) - 1
        return t["start"] if i < 0 else levels[i]

    out, framing, busy_until = [], 0, -1
    for et, lvl in zip(times, levels):
        if lvl != 0 or et < busy_until:
            continue
        val = 0
        for b in range(8):
            if level_at(et + (1.5 + b) * period):
                val |= 1 << b
        if not level_at(et + 9.5 * period):
            framing += 1
        out.append(val)
        busy_until = et + 9.3 * period
    return bytes(out), framing


def show(b):
    s = []
    for c in b:
        s.append({13: "<CR>", 10: "<LF>"}.get(c) or (chr(c) if 32 <= c < 127 else f"<{c:02X}>"))
    return "".join(s)


def printable(b):
    return sum(1 for c in b if 32 <= c < 127 or c in (10, 13)) / len(b) if b else 0.0


def report(scan):
    fcpu = scan["fcpu"]
    phases = {0: "starting", 1: "normal pass", 2: "swapped pass", 3: "finished, listening",
              4: "sniffing all ports", 5: "UART loopback"}
    print(f"Fcpu {fcpu} Hz | phase: {phases.get(scan['phase'], scan['phase'])} | "
          f"listening for {scan['listen_ms'] / 1000:.1f} s")
    if scan["skipped"]:
        print("swapped pass skipped: the normal wiring already answered")
    if scan["phase"] != 0:
        print("\nhardware UART loopback (256 byte values + text):")
        for r in scan["loops"]:
            if not r["sent"]:
                continue
            verdict = "PASS" if r["matched"] == r["sent"] and r["sent"] else "FAIL"
            pins = {5: "UART5 PC5->PC4", 1: "UART1 PC0->PC1"}.get(r["uart"], "?")
            line = f"  {pins} {r['baud']:>6} baud: {verdict}  sent {r['sent']}, received {r['received']}, matched {r['matched']}"
            if r["bad_i"] != 0xFFFF:
                line += f"; first bad byte #{r['bad_i']}: sent 0x{r['bad_s']:02X}, got " + \
                        ("nothing" if r["received"] == 0 else f"0x{r['bad_g']:02X}")
            print(line)
            if r["text"]:
                print(f"         text came back as: {show(r['text'])}")
    print("\npin connectivity (driven for 0.2 ms, then released with the pull-up off):")
    for p in scan["probes"]:
        if p["pin"] == 0:
            continue
        drive = "high" if p["drive"] else "low"
        if p["change"] == 0xFFFFFFFF:
            moved = f"stayed {'high' if p['first'] else 'low'} for 100 ms"
        else:
            moved = f"changed after {p['change'] * 1e6 / fcpu:.0f} us"
        first = "high" if p["first"] else "low"
        print(f"  PC{p['pin']} driven {drive:4s} -> released reads {first:4s}, {moved}; "
              f"with pull-up: {'high' if p['pulled'] else 'low'}")

    if scan["phase"] in (1, 2, 3):
        print("\n\"wisun get wisun\" sent on PC5 at 115200; edges seen on every other input pad:")
        hits = [(f"P{'ABCDEF'[i // 16]}{i % 16}", c) for i, c in enumerate(scan["sniff_counts"]) if c]
        print("  " + (", ".join(f"{name}: {c}" for name, c in hits) if hits else "none on any pad"))
        if scan["sniff_port"] != 0xFF:
            t = dict(scan["sniff"])
            print(f"  busiest pad P{'ABCDEF'[scan['sniff_port']]}{scan['sniff_pin']}, reply to \"\\r\\n\" recorded there:")
            report_trial(t, fcpu, f"TX PC5 -> RX P{'ABCDEF'[scan['sniff_port']]}{scan['sniff_pin']}  UART5 hw TX   115200 baud")
    print()
    for t in scan["trials"][:scan["n_trials"]]:
        if t["baud"] == 0:
            continue                                   # not run
        report_trial(t, fcpu, f"TX PC{t['tx']} -> RX PC{t['rx']}  {METHODS[t['method']]:13s} {t['baud']:>6} baud")

    if scan["phase"] == 3:
        print(f"\nlistening on every input pad for {scan['listen_ms'] / 1000:.1f} s; edges since the scan:")
        hits = [(f"P{'ABCDEF'[i // 16]}{i % 16}", c) for i, c in enumerate(scan["listen_counts"]) if c]
        print("  " + (", ".join(f"{name}: {c}" for name, c in hits) if hits else "none on any pad"))
        if scan["hit_port"] != 0xFF:
            pad = f"P{'ABCDEF'[scan['hit_port']]}{scan['hit_pin']}"
            report_trial(scan["hit"], fcpu, f"first pad to move: {pad}, recorded for 1.5 s")


def report_trial(t, fcpu, label):
    times, levels = transitions(t)
    line = f"  {label}: {t['edges']:5d} edges"
    if t["loop"]:
        line += f"  (sample every {t['loop'] * 1e6 / fcpu:.1f} us)"
    print(line)
    if len(times) < 2:
        return
    widths = [b - a for a, b in zip(times, times[1:]) if b - a < 10 ** 7]
    if widths:
        narrow = sorted(widths)[:max(1, len(widths) // 20)]
        est = fcpu / (sum(narrow) / len(narrow))
        print(f"      narrowest pulses imply ~{est:.0f} baud")
    candidates = ([t["baud"]] if t["baud"] else []) + [b for b in STANDARD_BAUDS if b != t["baud"]]
    results = []
    for baud in candidates:
        data, framing = uart_decode(t, times, levels, fcpu / baud)
        results.append((printable(data) - framing / max(1, len(data)), baud, data, framing))
    own = results[0] if t["baud"] else None
    best = max(results, key=lambda r: r[0])
    for tag, r in (("as sent", own), ("best fit", best if best is not own else None)):
        if r:
            _, baud, data, framing = r
            print(f"      {tag:8s} {baud:>6}: {len(data)} bytes, {framing} framing errors: {show(data)[:200]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", default=str(ROOT / "build" / "app.elf"))
    ap.add_argument("--dump", help="decode this saved dump instead of reading the target")
    ap.add_argument("--save", help="also save the raw dump here")
    args = ap.parse_args()

    addr, size = symbol(args.elf, "e51_scan")
    data = Path(args.dump).read_bytes() if args.dump else read_target(addr, size)
    if args.save:
        Path(args.save).write_bytes(data)
    scan = parse(data)
    if scan["magic"] != 0xE51C0DE5:
        sys.exit(f"no scan results yet (magic 0x{scan['magic']:08X}) -- is e51_scan flashed and running?")
    report(scan)


if __name__ == "__main__":
    main()
