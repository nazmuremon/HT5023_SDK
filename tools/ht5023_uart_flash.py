#!/usr/bin/env python3
"""Program an HT5023 application over a USB-UART adapter, through the bootloader.

The chip has no factory ISP, so bootloader/main.c has to be flashed once over
SWD (make bootloader, then the J-Link). After that, application updates go over
UART2 -- PC11 = TX2 -> adapter RX, PC12 = RX2 <- adapter TX, GND common.

    python tools/ht5023_uart_flash.py --port COM11 --file build/app.bin
    python tools/ht5023_uart_flash.py --port COM11 --file build/app.bin --reset

Build the application for this layout with:

    make EXAMPLE=text_back BOOT_APP=1

The bootloader only listens for its magic string in a short window after reset,
so either pass --reset (if the adapter's DTR/RTS is wired to the reset pin) or
power-cycle / press reset when prompted.
"""

import argparse
import sys
import time

MAGIC = b"HT5023BOOT"
# Cover the bootloader's whole post-reset entry window (BOOT_WINDOW_MS, 1.5 s)
# with a margin, so the magic is still arriving when it decides what to do.
HOLD_S = 2.5
DEFAULT_ADDR = 0x2000
CHUNK = 256


def open_port(port, baud):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is required:  pip install pyserial")
    try:
        return serial.Serial(port, int(baud), timeout=0.2)
    except Exception as e:
        sys.exit(f"cannot open {port}: {e}")


def list_ports():
    try:
        from serial.tools import list_ports as lp
    except ImportError:
        return []
    return [(p.device, p.description or "") for p in lp.comports()]


def read_line(ser, timeout=2.0):
    """One CRLF-terminated line from the bootloader, or '' on timeout."""
    end = time.time() + timeout
    buf = bytearray()
    while time.time() < end:
        b = ser.read(1)
        if not b:
            continue
        if b in (b"\r", b"\n"):
            if buf:
                return buf.decode("utf-8", "replace")
            continue
        buf += b
    return ""


def command(ser, text, timeout=3.0):
    ser.reset_input_buffer()
    ser.write(text.encode() + b"\n")
    return read_line(ser, timeout)


def pulse_reset(ser, log=print):
    """Reset the target if the adapter's DTR/RTS reaches the reset pin."""
    log("pulsing DTR/RTS to reset the target")
    for line in ("dtr", "rts"):
        try:
            setattr(ser, line, True)
        except Exception:
            pass
    time.sleep(0.05)
    for line in ("dtr", "rts"):
        try:
            setattr(ser, line, False)
        except Exception:
            pass
    time.sleep(0.05)


def enter_bootloader(ser, seconds=20.0, do_reset=False, log=print):
    """Hold a stream of magic on the wire until the bootloader answers.

    The bootloader only listens for a short window after reset, so the magic has
    to already be arriving when the board comes up -- sending it once every few
    hundred ms loses that race. This pushes it out every 25 ms for `seconds`,
    which is long enough to press the reset button by hand.
    """
    if do_reset:
        pulse_reset(ser, log=log)
    else:
        log(f"PRESS RESET on the board now -- listening for {seconds:.0f}s")

    end = time.time() + seconds
    seen = ""
    while time.time() < end:
        ser.write(MAGIC + b"\n")
        line = read_line(ser, 0.025)
        while line:
            if "HT5023 bootloader" in line:
                seen = line
            elif "starting application" in line:
                seen = ""            # too late: it already launched the app
            line = read_line(ser, 0.02)
        if seen:
            # The banner only means the board just booted -- the entry window
            # is still open and still has to be won. Checking with 'I' first
            # goes quiet for the drain, the window closes mid-drain and the
            # target launches the application, so the magic is held across the
            # whole window before anything else is said.
            hold = time.time() + HOLD_S
            while time.time() < hold:
                ser.write(MAGIC + b"\n")
                time.sleep(0.02)
                ser.read(256)
            if confirm_in_bootloader(ser):
                return seen
            seen = ''      # missed it; keep streaming for the next reset
    return ""


def drain(ser, quiet=0.35, limit=3.0):
    """Swallow whatever is still coming back.

    Entry spams the magic every 25 ms and the target answers each one with a
    banner, so a queue of stale replies is waiting when we start issuing
    commands -- the first erase would read a banner instead of its own OK.
    """
    end = time.time() + limit
    last = time.time()
    while time.time() < end and time.time() - last < quiet:
        if ser.read(256):
            last = time.time()
    ser.reset_input_buffer()


def confirm_in_bootloader(ser):
    """Prove we are in the command loop, not just reading a stale banner."""
    drain(ser)
    reply = command(ser, "I", timeout=1.5)
    if "HT5023 bootloader" not in reply:
        return False
    drain(ser)          # the I reply may itself be followed by queued banners
    return True


def program(ser, image, addr, log=print, progress=None):
    """Erase and write `image` at `addr`, verifying every chunk.

    Returns "" on success or a message describing what went wrong. `progress`
    is called with (bytes_done, total) so a UI can show a bar.
    """
    drain(ser)
    log(f"erasing {len(image)} bytes at {addr:#010x}")
    reply = command(ser, f"E {addr:x} {len(image):x}", timeout=30.0)
    while "HT5023 bootloader" in reply:      # a queued banner, not our answer
        reply = read_line(ser, 5.0)
    if not reply.startswith("OK"):
        if "starting application" in reply:
            return ("the bootloader launched the application before the erase -- "
                    "press reset while the tool is waiting")
        return f"erase failed: {reply or '(no reply)'}"

    sent = 0
    for off in range(0, len(image), CHUNK):
        piece = bytearray(image[off:off + CHUNK])
        while len(piece) % 4:
            piece.append(0xFF)              # the target pads the same way
        want = sum(piece) & 0xFFFF

        for attempt in range(3):
            ser.reset_input_buffer()
            ser.write(f"W {addr + off:x} {len(piece):x}\n".encode())
            ser.write(bytes(piece))
            reply = read_line(ser, 5.0)
            if reply.startswith("OK"):
                try:
                    got = int(reply.split()[1], 16)
                except (IndexError, ValueError):
                    got = -1
                if got == want:
                    break
                reply = f"checksum {got:#06x} != {want:#06x}"
            if attempt == 2:
                return f"write failed at {addr + off:#x}: {reply or '(no reply)'}"

        sent += len(piece)
        if progress:
            progress(min(sent, len(image)), len(image))
    return ""


def jump(ser, log=print):
    reply = command(ser, "J", timeout=3.0)
    log(reply if reply else "(no reply to J)")
    return reply.startswith("GO")


def upload(port, baud, image, addr=DEFAULT_ADDR, log=print, progress=None,
           do_reset=False, run_after=True, seconds=20.0):
    """Whole flow against an already-open or named port. Returns "" on success."""
    ser = port if hasattr(port, "write") else open_port(port, baud)
    try:
        if not enter_bootloader(ser, seconds=seconds, do_reset=do_reset, log=log):
            return ("could not reach the bootloader -- is it installed, is the "
                    "adapter on PC11/PC12, and did the board reset?")
        err = program(ser, image, addr, log=log, progress=progress)
        if err:
            return err
        if run_after and not jump(ser, log=log):
            return "programmed, but the target did not confirm the jump"
        return ""
    finally:
        if not hasattr(port, "write"):
            ser.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: first USB-UART found)")
    ap.add_argument("--baud", default=115200, type=int)
    ap.add_argument("--file", default="build/app.bin", help="application binary")
    ap.add_argument("--addr", default=hex(DEFAULT_ADDR),
                    help=f"flash address (default {hex(DEFAULT_ADDR)})")
    ap.add_argument("--reset", action="store_true",
                    help="pulse DTR/RTS to reset the target before connecting")
    ap.add_argument("--no-run", action="store_true",
                    help="leave the target in the bootloader instead of starting the app")
    args = ap.parse_args()

    port = args.port
    if not port:
        hints = ("ftdi", "ft232", "cp210", "ch340", "usb serial", "usb-serial")
        for dev, desc in list_ports():
            if any(h in desc.lower() for h in hints):
                port = dev
                print(f"using {dev} ({desc})")
                break
    if not port:
        sys.exit("no USB-UART adapter found; pass --port")

    try:
        image = open(args.file, "rb").read()
    except OSError as e:
        sys.exit(f"cannot read {args.file}: {e}")
    addr = int(args.addr, 0)
    print(f"{args.file}: {len(image)} bytes -> {addr:#010x}")

    ser = open_port(port, args.baud)
    err = upload(ser, args.baud, image, addr,
                 log=lambda m: print("   " + m),
                 progress=lambda done, total: print(
                     f"\r   writing {done:6d}/{total} bytes  {done * 100 // total:3d}%",
                     end="", flush=True),
                 do_reset=args.reset, run_after=not args.no_run)
    print()
    ser.close()
    if err:
        sys.exit(err)
    print("done" if not args.no_run else "done (left in the bootloader)")


if __name__ == "__main__":
    main()
