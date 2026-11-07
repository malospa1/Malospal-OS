"""Verify actual QEMU screen pixels and boot fallbacks, without guest test hooks."""

import json
import socket
import subprocess
import time

from build import qemu_command, qemu_path, stop_vm
from console_test import Console, require

BACKGROUND = bytes((0x10, 0x14, 0x18))
FOREGROUND = bytes((0xdc, 0xe6, 0xea))
# Independent probes from the public-domain font, not a screenshot OCR dependency.
GLYPHS = {
    "M": (0x63, 0x77, 0x7f, 0x7f, 0x6b, 0x63, 0x63, 0),
    "K": (0x67, 0x66, 0x36, 0x1e, 0x36, 0x66, 0x67, 0),
    "m": (0, 0, 0x33, 0x7f, 0x7f, 0x6b, 0x63, 0),
    "a": (0, 0, 0x1e, 0x30, 0x3e, 0x33, 0x6e, 0),
    "l": (0x0e, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x1e, 0),
    "p": (0, 0, 0x3b, 0x66, 0x66, 0x3e, 0x06, 0x0f),
    "y": (0, 0, 0x33, 0x33, 0x33, 0x3e, 0x30, 0x1f),
    "o": (0, 0, 0x1e, 0x33, 0x33, 0x33, 0x1e, 0),
    "s": (0, 0, 0x3e, 3, 0x1e, 0x30, 0x1f, 0),
    ">": (6, 12, 24, 48, 24, 12, 6, 0),
}


def listener():
    result = socket.socket()
    result.bind(("127.0.0.1", 0))
    result.listen(1)
    result.settimeout(60)
    return result


class Monitor:
    def __init__(self, connection):
        connection.settimeout(10)
        self.stream = connection.makefile("rwb", buffering=0)
        require("QMP" in json.loads(self.stream.readline()), "QMP greeting")
        self.execute("qmp_capabilities")

    def execute(self, command, **arguments):
        if command == "screendump" and "filename" in arguments:
            arguments["filename"] = qemu_path(arguments["filename"])
        self.stream.write(json.dumps({"execute": command, "arguments": arguments}).encode() + b"\n")
        while True:
            message = json.loads(self.stream.readline())
            if "error" in message:
                raise RuntimeError(f"QMP {command}: {message['error']}")
            if "return" in message:
                return message["return"]

    def snapshot(self, path):
        self.execute("screendump", filename=str(path), format="ppm")
        header, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
        width, height = map(int, dimensions.split())
        require(header == b"P6" and maximum == b"255" and len(pixels) == width * height * 3, "PPM format")
        return width, height, pixels


def text_at(snapshot, text, column, row):
    width, height, pixels = snapshot
    if width < 16 + (column + len(text)) * 8 or height < 16 + (row + 1) * 16:
        return False
    for offset, ch in enumerate(text):
        for y in range(16):
            bits = GLYPHS[ch][y // 2]
            for x in range(8):
                pos = ((16 + row * 16 + y) * width + 16 + (column + offset) * 8 + x) * 3
                expected = FOREGROUND if bits & (1 << x) else BACKGROUND
                if pixels[pos:pos + 3] != expected:
                    return False
    return True


def check_screen(iso, memory, firmware=None, firmware_vars=None, uefi=False, serial=True, video=True, keyboard=True):
    mode = "uefi" if firmware or uefi else "bios"
    kind = "normal" if serial and video else "no-serial" if video else "no-video"
    if not serial and not keyboard:
        kind = "no-input"
    base = iso.parent / f"screen-{mode}-{memory}-{kind}"
    with listener() as qmp, listener() as uart, base.with_suffix(".log").open("wb") as stream, \
            base.with_suffix(".qemu.log").open("wb") as errors:
        command = qemu_command(iso, memory, False, firmware, firmware_vars, uefi)
        command[command.index("-serial") + 1] = f"tcp:127.0.0.1:{uart.getsockname()[1]}" if serial else "none"
        command += ["-qmp", f"tcp:127.0.0.1:{qmp.getsockname()[1]}"]
        if not video:
            command += ["-vga", "none"]
        if not keyboard:
            command[command.index("-machine") + 1] = "q35,i8042=off"
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=errors, stderr=subprocess.STDOUT)
        try:
            connection, _ = qmp.accept()
            with connection:
                monitor = Monitor(connection)
                try:
                    if serial:
                        serial_connection, _ = uart.accept()
                        with serial_connection:
                            serial_connection.settimeout(0.2)
                            guest = Console(serial_connection, process, stream)
                            boot = guest.receive()
                            require("MALOSPAL_KERNEL_OK" in boot, "screen test boot")
                            if not video:
                                require("No supported framebuffer" in boot, "serial-only fallback")
                                guest.ask(b"ticks\n", "TIMER ticks=")
                            else:
                                require("[ok] Screen output:" in boot, "framebuffer initialization")
                                time.sleep(0.1)
                                before = monitor.snapshot(base.with_suffix(".ppm"))
                                require(text_at(before, "Ma", 0, 1), "visible kernel banner")
                                require(any(text_at(before, "malospal>", 0, y) for y in range(46)), "visible prompt")
                                monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                                guest.ask(b"echo x\n" * 60, "x\n", prompts=60)
                                time.sleep(0.1)
                                after = monitor.snapshot(base.with_name(base.name + "-scroll.ppm"))
                                require(after != before, "screen changed after output")
                                require(text_at(after, "malospal>", 0, (after[1] - 32) // 16 - 1), "prompt after scrolling")
                                monitor.execute("screendump", filename=str(base.with_name(base.name + "-scroll.png")), format="png")
                    else:
                        deadline = time.monotonic() + 60
                        while time.monotonic() < deadline:
                            require(process.poll() is None, "screen-only QEMU exited")
                            frame = monitor.snapshot(base.with_suffix(".ppm"))
                            marker = "malospal>" if keyboard else "K"
                            if text_at(frame, "Ma", 0, 1) and any(text_at(frame, marker, 0, y) for y in range(46)):
                                break
                            time.sleep(0.2)
                        else:
                            raise RuntimeError("Screen-only boot did not reach its ready message")
                        monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                finally:
                    monitor.stream.close()
        finally:
            stop_vm(process)
    print(f"PASS: screen {kind}, {mode.upper()}, {memory} MiB; {base}", flush=True)
