#!/usr/bin/env python3
"""LCD segment mapper for the energy-meter PCB's 29-pin glass (U11).

Lights one glass SEG x COM at a time and records what you see, in a browser
page with a drawing of the display.

    python3 tools/lcd_mapper/lcd_mapper.py            # opens http://localhost:8766/
    python3 tools/lcd_mapper/lcd_mapper.py --jlink    # drive it over the J-Link
    python3 tools/lcd_mapper/lcd_mapper.py --serial /dev/ttyUSB0 --no-browser

Two ways to reach the board, picked in the page (or by the flags above):

  J-Link (SWD)  No Serial needed. The CPU is halted, so meter_lcd stops
                redrawing, and the display buffer is written straight over SWD;
                the LCD controller keeps scanning on its own 32 kHz clock.
                meter_lcd must have run once to set the controller up. The
                watchdog runs from that clock too, so the tool feeds it while
                the CPU is halted. Disconnecting lets meter_lcd run on.
  Serial        The USB adapter on J6, through meter_lcd's m / n / p commands.
                Close the Flasher's serial monitor first -- one program per port.

Every answer is saved as you go to sketches/meter_lcd/segment_map.json.
Listens on 127.0.0.1 only: it drives hardware. Serial needs pyserial.
"""
import argparse, glob, json, os, pty, re, shutil, subprocess, termios, threading, time, webbrowser
from collections import deque
from datetime import datetime
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # the J-Link path and the page still work
    serial = None

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
RESULTS = os.path.join(REPO, "sketches", "meter_lcd", "segment_map.json")
SKETCH = os.path.join(REPO, "sketches", "meter_lcd", "meter_lcd.ino")
BAUD = 115200
POSITIONS = 21 * 8
JLINK_PORT = "jlink"

# What meter_lcd prints after every m / n / p:  "map 37: glass SEG5 (PF7) COM 6*"
MAP_LINE = re.compile(r"^map (\d+): glass SEG(\d+) \((\w+)\) COM (\d)\*")
# Commands the page may send: all on, blank, line check.
RAW_OK = {"t", "b", "k"}

# LCD controller (see meter_lcd.ino): one 32-bit buffer slot per controller SEG,
# its COM bits in the low byte. Word writes only -- byte writes land in the
# wrong slot on this part.
LCD_CLK = 0x4000D000
LCD_BUF = 0x4000D010
LCD_CLK_DUTY_MASK, LCD_CLK_DUTY_8 = 0x18, 0x10   # meter_lcd runs 1/8 duty; bias is its contrast
DHCSR = 0xE000EDF0       # Cortex-M debug halting control/status
DHCSR_S_HALT = 1 << 17
WDT_CLR = 0x40010004     # WDTCLR: feed key 0xAA in the HIGH byte, timeout SET below
WDT_FEED_S = 1.0         # feed interval; the default timeout is 4.16 s


def glass_table():
    """[(controller SEG, pad)] for glass SEG1..21, read from meter_lcd.ino's
    GLASS_SEG table so the two cannot drift apart."""
    src = open(SKETCH, encoding="utf-8").read()
    rows = re.findall(r'\{\{GPIO[A-F]_BASE,\s*\d+u,\s*\du\},\s*(\d+)u,\s*"(P[A-F]\d+)"\},\s*//\s*SEG(\d+)', src)
    table = {int(g): (int(slot), pad) for slot, pad, g in rows}
    if sorted(table) != list(range(1, 22)):
        raise SystemExit(f"could not read the 21-entry GLASS_SEG table from {SKETCH}")
    return [table[g] for g in range(1, 22)]


GLASS = glass_table()


def map_line(index):
    seg, com = index // 8 + 1, index % 8 + 1
    return f"map {index}: glass SEG{seg} ({GLASS[seg - 1][1]}) COM {com}*"


def find_jlink_exe():
    exe = shutil.which("JLinkExe")
    if exe:
        return exe
    for d in sorted(glob.glob("/opt/SEGGER/JLink*"), reverse=True):   # newest first
        if os.access(os.path.join(d, "JLinkExe"), os.X_OK):
            return os.path.join(d, "JLinkExe")
    return None


def jlink_plugged_in():
    for f in glob.glob("/sys/bus/usb/devices/*/idVendor"):
        try:
            if open(f).read().strip() == "1366":      # SEGGER
                return True
        except OSError:
            pass
    return False


class SerialLink:
    """meter_lcd over the USB adapter. A reader thread keeps every line,
    numbered, so a reply can be told apart from something printed earlier."""

    def __init__(self, log):
        self.log = log
        self.ser = None
        self.error = ""
        self.cond = threading.Condition()
        self.write_lock = threading.Lock()

    def connect(self, port):
        if serial is None:
            self.error = "pyserial is not installed: pip install pyserial"
            return False
        try:
            self.ser = serial.Serial(port, BAUD, timeout=0.1)
        except (serial.SerialException, OSError) as e:
            msg = str(e)
            if "busy" in msg.lower() or "Errno 16" in msg:
                msg = f"{port} is busy -- close the Flasher's serial monitor (or any terminal) and retry"
            elif "Permission" in msg:
                msg = f"no permission for {port} -- add yourself to the dialout group"
            self.error = msg
            return False
        threading.Thread(target=self._reader, args=(self.ser,), daemon=True).start()
        return True

    def close(self):
        ser, self.ser = self.ser, None
        if ser:
            try:
                ser.close()
            except Exception:
                pass

    def connected(self):
        return self.ser is not None

    def _reader(self, ser):
        buf = b""
        while self.ser is ser:
            try:
                chunk = ser.read(256)
            except (serial.SerialException, OSError, TypeError):
                self.error = "lost the serial port"
                self.ser = None
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                with self.cond:
                    self.log.add(raw.decode("utf-8", "replace").rstrip("\r"))
                    self.cond.notify_all()

    def _send(self, text):
        if not self.ser:
            raise IOError("board not connected")
        with self.write_lock:
            self.ser.write(text.encode("ascii"))

    def _request(self, text, reply, timeout=1.2, tries=2):
        """meter_lcd's UART holds one byte and loses anything that arrives while
        it prints, so a missed command is simply sent again."""
        for _ in range(tries):
            with self.cond:
                start = self.log.next_no
            self._send(text)
            deadline = time.monotonic() + timeout
            with self.cond:
                while True:
                    for no, line in self.log.since(start):
                        m = reply.match(line)
                        if m:
                            return m
                    left = deadline - time.monotonic()
                    if left <= 0:
                        break
                    self.cond.wait(left)
        return None

    def show(self, index):
        try:
            m = self._request(f"{index}m\n", MAP_LINE)
        except IOError as e:
            return {"ok": False, "error": str(e)}
        if not m:
            return {"ok": False, "error": "no 'map' reply -- is the updated meter_lcd flashed and running?"}
        return {"ok": int(m.group(1)) == index, "line": m.group(0)}

    def raw(self, cmd):
        try:
            self._send(cmd + "\n")
        except IOError as e:
            return {"ok": False, "error": str(e)}
        return {"ok": True}


class JLinkLink:
    """The display buffer written straight over SWD, through one long-lived
    JLinkExe on a pseudo-terminal (it only prints its 'J-Link>' prompt to a
    terminal). Replies are matched by their CONTENT -- 'Writing 00000002 ->
    4000D014', '4000D014 = 00000002' -- not by counting prompts: JLinkExe
    sometimes prints an extra one, and counting then reads every later reply
    one command late."""

    PROMPT = b"J-Link>"

    def __init__(self, log):
        self.log = log
        self.proc = None
        self.fd = None
        self.buf = bytearray()
        self.cond = threading.Condition()
        self.lock = threading.Lock()          # one command at a time
        self.error = ""
        self.dead = False
        self.wdt_set = 0x40

    def connect(self, _port=None):
        exe = find_jlink_exe()
        if not exe:
            self.error = "JLinkExe not found -- install the SEGGER J-Link software"
            return False
        master, slave = pty.openpty()
        attrs = termios.tcgetattr(slave)
        attrs[3] &= ~termios.ECHO             # no echo of what we type
        termios.tcsetattr(slave, termios.TCSANOW, attrs)
        self.proc = subprocess.Popen(
            [exe, "-device", "Cortex-M0", "-if", "SWD", "-speed", "1000", "-autoconnect", "1", "-NoGui", "1"],
            stdin=slave, stdout=slave, stderr=slave, close_fds=True)
        os.close(slave)
        self.fd, self.buf = master, bytearray()
        threading.Thread(target=self._reader, args=(master,), daemon=True).start()

        # The first prompt comes once JLinkExe has attached; anything sent before
        # it would be answered with the connect banner.
        deadline = time.monotonic() + 10
        with self.cond:
            while self.PROMPT not in self.buf and self.proc.poll() is None:
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                self.cond.wait(left)
        if not self._halt():                   # meter_lcd stops redrawing
            text = self.buf.decode(errors="replace")
            hint = next((l.strip() for l in text.splitlines()
                         if re.search(r"cannot|could not|failed|error|not found", l, re.I)), "")
            tail = [l.strip() for l in text.replace("\r", "").splitlines() if l.strip()][-6:]
            self.log.add("J-Link connect failed; JLinkExe said: " + " | ".join(tail))
            self.close(resume=False)
            self.error = f"J-Link did not reach the target{': ' + hint if hint else ''}"
            return False
        clr = self._read16(WDT_CLR)
        if clr >= 0:
            self.wdt_set = clr & 0xFF          # keep meter_lcd's timeout, just feed it
        self._feed()
        threading.Thread(target=self._keep_fed, daemon=True).start()
        clk = self._read16(LCD_CLK)
        if clk < 0 or clk & LCD_CLK_DUTY_MASK != LCD_CLK_DUTY_8:
            self.log.add(f"WARNING: LCDCLK=0x{clk & 0xFFFF:04X} is not 1/8 duty -- the LCD controller "
                         "is not set up; flash meter_lcd and let it boot")
        self.log.add("J-Link: CPU halted -- meter_lcd paused, display buffer written over SWD, "
                     "watchdog fed every second")
        self.error = ""
        return True

    def _reader(self, fd):
        while True:
            try:
                b = os.read(fd, 4096)
            except OSError:
                break
            if not b:
                break
            with self.cond:
                self.buf.extend(b)
                self.cond.notify_all()

    def _abandon(self, why):
        """A JLinkExe that stops answering never recovers -- a probe that
        re-enumerates on USB leaves it waiting for good. Kill it so the board
        reconnects, instead of every command waiting out its timeout while
        the page queues up behind it."""
        if not self.dead:
            self.dead = True
            self.error = why
            self.log.add(f"J-Link: {why}")
            try:
                self.proc.kill()
            except Exception:
                pass

    def _expect(self, line, pattern, timeout=1.0):
        """Send one command and wait for the reply that proves it ran. They
        take 1-10 ms, so a second without one means the session is gone."""
        rx = re.compile(pattern, re.I)
        with self.lock:
            if self.proc is None or self.proc.poll() is not None:
                return None
            with self.cond:
                if len(self.buf) > (1 << 18):
                    del self.buf[:-(1 << 16)]
                start = len(self.buf)
            os.write(self.fd, (line + "\n").encode())
            deadline = time.monotonic() + timeout
            with self.cond:
                while True:
                    m = rx.search(self.buf[start:].decode(errors="replace"))
                    if m:
                        return m
                    if self.proc.poll() is not None:
                        self._abandon("JLinkExe exited")
                        return None
                    left = deadline - time.monotonic()
                    if left <= 0:
                        self._abandon(f"no answer to '{line.split()[0]}' -- probe unplugged or reset?")
                        return None
                    self.cond.wait(left)

    def _read32(self, addr):
        m = self._expect(f"mem32 0x{addr:08X}, 1", rf"{addr:08X} = ([0-9A-F]{{8}})")
        return int(m.group(1), 16) if m else None

    def _write32(self, addr, value):
        return self._expect(f"w4 0x{addr:08X}, 0x{value:08X}",
                            rf"Writing {value:08X} -> {addr:08X}") is not None

    def _halt(self):
        """Halted, whatever state the CPU was in -- a watchdog reset, say."""
        v = self._read32(DHCSR)
        if v is not None and v & DHCSR_S_HALT:
            return True
        self._expect("h", r"PC = [0-9A-F]{8}", timeout=2)
        v = self._read32(DHCSR)
        return v is not None and bool(v & DHCSR_S_HALT)

    def _cmd(self, line, timeout=5):
        """Prompt-terminated command, used only on the way out (r, g)."""
        with self.lock:
            if self.proc is None or self.proc.poll() is not None:
                return None
            with self.cond:
                start = len(self.buf)
            os.write(self.fd, (line + "\n").encode())
            deadline = time.monotonic() + timeout
            with self.cond:
                while self.PROMPT not in self.buf[start:]:
                    left = deadline - time.monotonic()
                    if left <= 0 or self.proc.poll() is not None:
                        return None
                    self.cond.wait(left)
                end = self.buf.index(self.PROMPT, start)
                return self.buf[start:end].decode(errors="replace").replace("\r", "")

    def _feed(self):
        return self._expect(f"w2 0x{WDT_CLR:08X}, 0x{0xAA00 | self.wdt_set:04X}",
                            rf"Writing AA{self.wdt_set:02X} -> {WDT_CLR:08X}") is not None

    def _keep_fed(self):
        """meter_lcd keeps the 32 kHz clock on for the LCD, and the watchdog
        runs from it (JTAGWDTEN, PA6, is high unless J1 1-2 is jumpered). A
        halted CPU cannot feed it, so ~4.2 s after the halt it reset the chip --
        the beep, 12345678 on the glass, and a lost J-Link session, over and
        over. While this session holds the CPU, it feeds the dog itself."""
        while self.connected():
            time.sleep(WDT_FEED_S)
            if self.connected():
                self._feed()

    def _read16(self, addr):
        m = self._expect(f"mem16 0x{addr:08X}, 1", rf"{addr:08X} = ([0-9A-F]{{4}})")
        return int(m.group(1), 16) if m else -1

    def _fill(self, values):
        """Write all 21 glass slots. Every step rewrites all of them, so it does
        not matter what meter_lcd (or a reset) left in the buffer."""
        if not self._halt():
            raise IOError("could not halt the CPU over the J-Link")
        for (slot, _pad), value in zip(GLASS, values):
            if not self._write32(LCD_BUF + 4 * slot, value):
                raise IOError(f"J-Link write to SEG slot {slot} failed")

    def connected(self):
        return self.proc is not None and self.proc.poll() is None and not self.dead

    def show(self, index):
        seg, com = index // 8, index % 8
        values = [0] * 21
        values[seg] = 1 << com
        try:
            self._fill(values)
        except IOError as e:
            return {"ok": False, "error": str(e)}
        slot = GLASS[seg][0]
        got = self._read32(LCD_BUF + 4 * slot)
        if got != values[seg]:
            return {"ok": False, "error": f"read-back of SEG slot {slot}: wrote 0x{values[seg]:02X}, "
                                         f"read {'nothing' if got is None else f'0x{got:02X}'}"}
        line = map_line(index)
        self.log.add(line)
        return {"ok": True, "line": line}

    def raw(self, cmd):
        if cmd == "k":
            return {"ok": False, "error": "the line check runs in meter_lcd over Serial (k); "
                                          "connect the USB adapter on J6 to use it"}
        try:
            self._fill([0xFF if cmd == "t" else 0x00] * 21)
        except IOError as e:
            return {"ok": False, "error": str(e)}
        self.log.add("every segment on" if cmd == "t" else "blank")
        return {"ok": True}

    def close(self, resume=True):
        if self.connected():
            if resume:
                # Just let it go on -- no reset, no beep. meter_lcd redraws its
                # whole display within half a second, so the mapper's segment
                # does not linger.
                self._cmd("g")
                self.log.add("J-Link: CPU resumed, meter_lcd running again")
            try:
                os.write(self.fd, b"q\n")
                self.proc.wait(3)
            except (OSError, subprocess.TimeoutExpired):
                self.proc.kill()
        elif self.proc is not None:            # abandoned: already killed, just reap it
            try:
                self.proc.kill()
                self.proc.wait(2)
            except Exception:
                pass
        self.proc = None
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = None


class Log:
    """Numbered lines shared by both links, for the page and for replies."""

    def __init__(self):
        self.lines = deque(maxlen=400)
        self.next_no = 0
        self.lock = threading.Lock()

    def add(self, text):
        with self.lock:
            self.lines.append((self.next_no, text))
            self.next_no += 1

    def since(self, no):
        with self.lock:
            return [(n, t) for n, t in self.lines if n >= no]

    def tail(self, n=60):
        with self.lock:
            return [t for _, t in list(self.lines)[-n:]]


class Board:
    """Whichever link is in use -- Serial or J-Link -- behind one interface."""

    def __init__(self):
        self.log = Log()
        self.link = None
        self.port = None
        self.error = ""
        self.auto = True                      # off once the user disconnects by hand
        self.want = None                      # the port to keep (re)connected to
        self.retry_at = 0.0                   # no automatic retry before this
        self.active_tab = None                # the one browser tab allowed to drive
        self.lock = threading.RLock()

    def ports(self):
        found = []
        if jlink_plugged_in() and find_jlink_exe():
            found.append({"device": JLINK_PORT, "description": "SEGGER J-Link over SWD (halts the CPU)"})
        if serial is not None:
            for p in serial.tools.list_ports.comports():
                # Skip the motherboard's /dev/ttyS* stubs; keep USB adapters.
                if p.vid is not None or re.search(r"ttyUSB|ttyACM|cu\.|COM\d", p.device):
                    found.append({"device": p.device, "description": p.description or ""})
        return found

    def connect(self, port):
        with self.lock:
            if port == self.port and self.connected():
                return True                   # a second tab or a double click: already there
            self.close()
            self.want = port                  # keep trying this one, even if this attempt fails
            link = JLinkLink(self.log) if port == JLINK_PORT else SerialLink(self.log)
            if link.connect(port):
                self.link, self.port, self.want, self.error = link, port, port, ""
                return True
            self.error = link.error
            self.retry_at = time.monotonic() + 5  # don't hammer a failing probe from every poll
            return False

    def disconnect(self):
        with self.lock:
            self.auto, self.want = False, None
            self.close()

    def close(self):
        with self.lock:
            if self.link:
                self.link.close()
            self.link, self.port = None, None

    def connected(self):
        return self.link is not None and self.link.connected()

    def _ensure(self):
        """Connected -- or reconnected, if the session died under us."""
        if self.connected():
            return True
        if self.want and time.monotonic() >= self.retry_at:
            self.log.add(f"reconnecting to {self.want}")
            return self.connect(self.want)
        return False

    def auto_connect(self):
        """Run by every page poll: bring back a session that died, or take the
        only port there is. Never while another request holds the board --
        that is how one connect used to set off the next -- never after a
        manual Disconnect, and at most every 5 s after a failure."""
        if self.connected() or not self.auto or time.monotonic() < self.retry_at:
            return
        if not self.lock.acquire(blocking=False):
            return
        try:
            if self.connected():
                return
            if self.want:
                self._ensure()
                return
            ports = self.ports()
            if len(ports) == 1:
                self.connect(ports[0]["device"])
        finally:
            self.lock.release()

    def _run(self, fn):
        with self.lock:
            if not self._ensure():
                return {"ok": False, "error": self.error or "board not connected"}
            r = fn(self.link)
            if not r["ok"] and not self.connected() and self._ensure():
                r = fn(self.link)             # died mid-step: once more on a fresh session
            return r

    def show(self, index):
        return self._run(lambda link: link.show(index))

    def raw(self, cmd):
        return self._run(lambda link: link.raw(cmd))


board = Board()
results_lock = threading.Lock()


def load_results():
    try:
        with open(RESULTS, encoding="utf-8") as f:
            data = json.load(f)
    except (FileNotFoundError, ValueError):
        data = {}
    data.setdefault("glass", "ht5023_energy_meter_v1 U11, 21 SEG x 8 COM")
    data.setdefault("index", "position = (glass SEG - 1) * 8 + (COM star - 1)")
    data.setdefault("positions", {})
    data.setdefault("cursor", 0)
    return data


def results_display():
    rel = os.path.relpath(RESULTS, REPO)
    return RESULTS if rel.startswith("..") else rel


def save_results(data):
    os.makedirs(os.path.dirname(RESULTS), exist_ok=True)
    tmp = RESULTS + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp, RESULTS)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=HERE, **kw)

    def log_message(self, fmt, *args):  # keep the console for real news
        pass

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        try:
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass                               # the page gave up or reloaded

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return {}

    def do_GET(self):
        if self.path.split("?")[0] == "/api/state":
            tab = (re.search(r"[?&]tab=(\w+)", self.path) or [None, None])[1]
            board.auto_connect()
            with results_lock:
                data = load_results()
            return self._json(200, {
                "ports": board.ports(),
                "port": board.port,
                "jlink": board.port == JLINK_PORT,
                "active": board.active_tab is None or tab == board.active_tab,
                "connected": board.connected(),
                "error": board.error,
                "log": board.log.tail(),
                "results": data,
                "results_path": results_display(),
            })
        return super().do_GET()

    def do_POST(self):
        path, body = self.path.split("?")[0], self._body()
        if path == "/api/claim":
            board.active_tab = str(body.get("tab", "")) or None
            return self._json(200, {"ok": True})
        if board.active_tab and body.get("tab") != board.active_tab:
            return self._json(409, {"ok": False, "stale": True,
                                    "error": "the mapper is open in another tab -- that one drives the board"})
        if path == "/api/connect":
            board.auto = True
            ok = board.connect(str(body.get("port", "")))
            return self._json(200 if ok else 409, {"ok": ok, "error": board.error})
        if path == "/api/disconnect":
            board.disconnect()
            return self._json(200, {"ok": True})
        if path == "/api/show":
            index = int(body.get("index", 0)) % POSITIONS
            with results_lock:
                data = load_results()
                data["cursor"] = index
                save_results(data)
            return self._json(200, {"index": index, **board.show(index)})
        if path == "/api/raw":
            cmd = str(body.get("cmd", ""))
            if cmd not in RAW_OK:
                return self._json(400, {"ok": False, "error": f"not allowed: {cmd!r}"})
            return self._json(200, board.raw(cmd))
        if path == "/api/label":
            index = int(body.get("index", -1))
            if not 0 <= index < POSITIONS:
                return self._json(400, {"ok": False, "error": "bad index"})
            with results_lock:
                data = load_results()
                key = str(index)
                if body.get("clear"):
                    data["positions"].pop(key, None)
                else:
                    data["positions"][key] = {
                        "seg": index // 8 + 1,
                        "com": index % 8 + 1,
                        "status": str(body.get("status", "")),      # confirmed / different / named / nothing
                        "element": str(body.get("element", "")),    # e.g. big3.b, icon.L1, custom
                        "label": str(body.get("label", "")),
                        "note": str(body.get("note", "")),
                        "time": datetime.now().isoformat(timespec="seconds"),
                    }
                save_results(data)
            return self._json(200, {"ok": True, "results": data})
        return self._json(404, {"ok": False, "error": "unknown endpoint"})


class QuietServer(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        import sys
        if not isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            super().handle_error(request, client_address)


def main():
    global RESULTS
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8766, help="web page port (default 8766)")
    ap.add_argument("--jlink", action="store_true", help="connect over the J-Link at start")
    ap.add_argument("--serial", help="connect to this serial port at start, e.g. /dev/ttyUSB0")
    ap.add_argument("--results", default=RESULTS, help="where to save the answers (default: %(default)s)")
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    RESULTS = os.path.abspath(a.results)

    want = JLINK_PORT if a.jlink else a.serial
    if want:
        print(f"board: {want} -- {'connected' if board.connect(want) else board.error}", flush=True)
    else:
        board.auto_connect()
        print(f"board: {board.port or 'pick one in the page'}", flush=True)

    srv = QuietServer(("127.0.0.1", a.port), Handler)
    url = f"http://localhost:{a.port}/"
    print(f"LCD mapper running at {url}  (Ctrl+C to stop)", flush=True)
    print(f"answers are saved to {RESULTS}", flush=True)
    if not a.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        board.close()                          # over the J-Link this lets meter_lcd run on


if __name__ == "__main__":
    main()
