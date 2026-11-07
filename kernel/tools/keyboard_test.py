"""Send real emulated PS/2 keys through QMP; no guest-only test entry points."""

import re
import subprocess
import time

from build import qemu_command, stop_vm
from console_test import Console, memory_fields, require
from screen_test import Monitor, listener, text_at

KEYS = dict(zip("`-=[]\\;',./", ("grave_accent", "minus", "equal", "bracket_left", "bracket_right",
                                "backslash", "semicolon", "apostrophe", "comma", "dot", "slash")))
KEYS.update({" ": "spc", "\r": "ret", "\n": "ret", "\b": "backspace", "\t": "tab"})
SHIFTED = dict(zip("~!@#$%^&*()_+{}|:\"<>?", "`1234567890-=[]\\;',./"))


class Keyboard:
    def __init__(self, monitor):
        self.monitor = monitor

    def key(self, code, down):
        self.monitor.execute("input-send-event", events=[
            {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": code}}}])
        time.sleep(0.005)

    def tap(self, code):
        self.key(code, True)
        self.key(code, False)

    def text(self, text):
        for char in text:
            shift = char.isupper() or char in SHIFTED
            base = SHIFTED.get(char, char.lower())
            code = KEYS.get(base, base)
            if shift:
                self.key("shift", True)
            self.tap(code)
            if shift:
                self.key("shift", False)

    def clear(self, right=False):
        ctrl = "ctrl_r" if right else "ctrl"
        self.key(ctrl, True)
        self.tap("u")
        self.key(ctrl, False)


def wait_picture(monitor, process, path, predicate):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        require(process.poll() is None, "keyboard QEMU exited")
        frame = monitor.snapshot(path)
        if predicate(frame):
            return frame
        time.sleep(0.1)
    raise RuntimeError(f"Keyboard screen check timed out: {path}")


def visible(frame, text):
    return any(text_at(frame, text, 0, row) for row in range(min(128, (frame[1] - 32) // 16)))


def exercise(keys, guest, boot):
    original = memory_fields(boot)

    def ask(command, expected):
        keys.text(command)
        response = guest.receive()
        require(expected in response, f"PS/2 response {expected!r}: {response!r}")
        return response

    ask("info\r", "Input: PS/2 IRQ1 (US)")
    require(memory_fields(ask("mem\r", "PMM total=")) == original, "PS/2 memory changed")
    first = re.search(r"TIMER ticks=(\d+)", ask("ticks\r", "TIMER ticks="))
    second = re.search(r"TIMER ticks=(\d+)", ask("ticks\r", "TIMER ticks="))
    require(int(second[1]) > int(first[1]), "timer stopped during PS/2 input")
    ask("helx\bp\r", "Command list")
    keys.text("halt")
    keys.clear(right=True)
    ask("echo cancelled\r", "\ncancelled\n")
    ask("echo ABC xyz 0123456789 ~!@#$%^&*()_+{}|:\"<>?\r", "\nABC xyz 0123456789 ~!@#$%^&*()_+{}|:\"<>?\n")
    keys.text("echo ")
    keys.tap("caps_lock")
    keys.text("ab")
    keys.key("shift_r", True)
    keys.tap("c")
    keys.key("shift_r", False)
    keys.tap("caps_lock")
    ask("\r", "\nABc\n")
    for special in ("pause", "sysrq", "up", "f1"):
        keys.tap(special)
    ask("echo plain\r", "\nplain\n")
    keys.text("echo erasex")
    keys.tap("delete")
    keys.tap("kp_enter")
    require("\nerase\n" in guest.receive(), "extended Enter/Delete")
    keys.text("halt")
    keys.tap("esc")
    ask("\r", "[error] Line discarded:")
    ask("echo " + "x" * 122 + "\r", "\n" + "x" * 122 + "\n")
    ask("echo " + "x" * 123 + "\b\r", "[error] Line discarded:")
    # A source switch must not assemble a command from two different devices.
    guest.connection.sendall(b"ha")
    guest.receive(marker=b"ha")
    ask("lt\r", "Input source changed; current line discarded")
    keys.text("ha")
    guest.receive(marker=b"ha")
    guest.ask(b"lt\r", "Input source changed; current line discarded")
    guest.connection.sendall(b"ha")
    guest.receive(marker=b"ha")
    keys.text("lt")
    guest.receive(marker=b"current line discarded\r\n")
    keys.clear()
    response = ask("echo cleared\r", "\ncleared\n")
    require("\b" not in response, "Ctrl+U erased the diagnostic line")
    # CR/LF suppression belongs to the previous source, not a PS/2 Enter.
    ask("echo recovered\r", "\nrecovered\n")
    for i in range(20):
        ask(f"echo k{i}\r", f"\nk{i}\n")
    counters = ask("keyboard\r", "KEYBOARD bytes=")
    fields = re.search(r"KEYBOARD bytes=(\d+) decoded=(\d+) queued=(\d+) dropped=(\d+) errors=(\d+) irqs=(\d+) ready=(\d+) faulted=(\d+)", counters)
    require(fields is not None, "keyboard counters missing")
    count, decoded, queued, dropped, errors, irqs, ready, faulted = map(int, fields.groups())
    require(count > 512 and decoded > 256 and irqs > 0 and dropped == errors == faulted == 0 and ready == 1,
            "keyboard IRQs, wrap, loss or fault counters")
    require(queued <= 3, "unconsumed keyboard input")  # Enter release can be pending.
    require(memory_fields(ask("mem\r", "PMM total=")) == original, "keyboard leaked memory")


def check_keyboard(iso, memory, firmware=None, firmware_vars=None, uefi=False, serial=True, keyboard=True):
    mode = "uefi" if firmware or uefi else "bios"
    kind = "normal" if serial and keyboard else "no-serial" if keyboard else "no-keyboard"
    base = iso.parent / f"keyboard-{mode}-{memory}-{kind}"
    with listener() as qmp, listener() as uart, base.with_suffix(".log").open("wb") as stream, \
            base.with_suffix(".qemu.log").open("wb") as errors:
        command = qemu_command(iso, memory, False, firmware, firmware_vars, uefi)
        command[command.index("-serial") + 1] = f"tcp:127.0.0.1:{uart.getsockname()[1]}" if serial else "none"
        command += ["-qmp", f"tcp:127.0.0.1:{qmp.getsockname()[1]}"]
        if not keyboard:
            command[command.index("-machine") + 1] = "q35,i8042=off"
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=errors, stderr=subprocess.STDOUT)
        try:
            connection, _ = qmp.accept()
            with connection:
                monitor = Monitor(connection)
                try:
                    keys = Keyboard(monitor)
                    if serial:
                        serial_connection, _ = uart.accept()
                        with serial_connection:
                            serial_connection.settimeout(0.2)
                            guest = Console(serial_connection, process, stream)
                            guest.deadline = time.monotonic() + 120
                            boot = guest.receive()
                            if keyboard:
                                require("[ok] PS/2 input ready" in boot, "PS/2 initialization")
                                exercise(keys, guest, boot)
                                monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                                keys.text("halt\r")
                                guest.receive(marker=b"Kernel halted.\r\n")
                            else:
                                require("[warn] PS/2 keyboard unavailable" in boot, "absent controller fallback")
                                guest.ask(b"ticks\n", "TIMER ticks=")
                                guest.ask(b"info\n", "Input: PS/2 unavailable")
                    else:
                        before = wait_picture(monitor, process, base.with_suffix(".ppm"), lambda f: visible(f, "malospal>"))
                        keys.text("echo Malospal\r")
                        after = wait_picture(monitor, process, base.with_suffix(".ppm"),
                                             lambda f: visible(f, "Malospal") and visible(f, "malospal>"))
                        require(before != after, "PS/2 without COM1 did not change screen")
                        monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                finally:
                    monitor.stream.close()
        finally:
            stop_vm(process)
    print(f"PASS: keyboard {kind}, {mode.upper()}, {memory} MiB; {base}", flush=True)
