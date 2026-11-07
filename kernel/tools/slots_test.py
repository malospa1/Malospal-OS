"""Exercise Reel Machine through the normal kernel, real PS/2 and screen pixels."""

import re
import socket
import subprocess
import time

from build import qemu_command, stop_vm
from console_test import Console, memory_fields, require
from game_test import colored_pixels
from keyboard_test import Keyboard, visible, wait_picture
from screen_test import Monitor, listener


def state(guest, wanted):
    data = bytearray()
    pattern = rb"SLOTS " + wanted.encode() + rb" score=(\d+) spins=(\d+) last=(\d+) reels=(\d),(\d),(\d)\r\n"
    while time.monotonic() < guest.deadline:
        require(guest.process.poll() is None, "slots QEMU stopped")
        try:
            part = guest.connection.recv(4096)
        except socket.timeout:
            continue
        require(part, "slots COM1 closed")
        guest.stream.write(part)
        guest.stream.flush()
        data.extend(part)
        require(b"[panic]" not in data, "panic in slots")
        match = re.search(pattern, data)
        if match:
            return tuple(map(int, match.groups()))
    raise RuntimeError(f"Slots {wanted} timed out: {data!r}")


def points(reels):
    if reels[0] == reels[1] == reels[2]:
        return (10, 20, 30, 50, 80, 150)[reels[0]]
    return 5 if len(set(reels)) == 2 else 0


def slot_visible(frame):
    return colored_pixels(frame, (0xf2, 0xf6, 0xf8)) > 20000


def reels(frame):
    width, height, pixels = frame
    reel_width = min((width - 152) // 3, 220)
    reel_height = min(height - 304, 300)
    left = (width - (3 * reel_width + 88)) // 2
    top = (height - reel_height) // 2
    return b"".join(pixels[((top + y) * width + left) * 3:((top + y) * width + left + 3 * reel_width + 24) * 3]
                    for y in range(reel_height))


def check_slots(iso, memory, firmware=None, firmware_vars=None, uefi=False, serial=True, video=True):
    mode = "uefi" if firmware or uefi else "bios"
    kind = "normal" if serial and video else "no-serial" if video else "no-video"
    base = iso.parent / f"slots-{mode}-{memory}-{kind}"
    with listener() as qmp, listener() as uart, base.with_suffix(".log").open("wb") as stream, \
            base.with_suffix(".qemu.log").open("wb") as errors:
        command = qemu_command(iso, memory, False, firmware, firmware_vars, uefi)
        command[command.index("-serial") + 1] = f"tcp:127.0.0.1:{uart.getsockname()[1]}" if serial else "none"
        command += ["-qmp", f"tcp:127.0.0.1:{qmp.getsockname()[1]}"]
        if not video:
            command += ["-vga", "none"]
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
                            original = memory_fields(guest.receive())
                            if not video:
                                guest.ask(b"slots\n", "Slots needs a framebuffer")
                                guest.ask(b"info\n", "Input: COM1 IRQ4")
                            else:
                                keys.text("slots\r")
                                require(state(guest, "idle") == (0, 0, 0, 0, 1, 3), "initial slots state")
                                ready = monitor.snapshot(base.with_suffix(".ppm"))
                                require(slot_visible(ready), "slot reels not visible")
                                keys.tap("spc")
                                require(state(guest, "spinning")[1] == 1, "first spin")
                                frame = monitor.snapshot(base.with_suffix(".ppm"))
                                time.sleep(0.2)
                                require(reels(frame) != reels(monitor.snapshot(base.with_suffix(".ppm"))), "reels not animated")
                                keys.text("   r  ")
                                settled = state(guest, "idle")
                                require(settled[1] == 1, "busy spin/reset changed spin count")
                                require(settled[0] == settled[2] == points(settled[3:]), "first award")
                                idle = monitor.snapshot(base.with_suffix(".ppm"))
                                time.sleep(0.25)
                                require(idle == monitor.snapshot(base.with_suffix(".ppm")), "settled reels still moving")
                                monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                                keys.tap("ret")
                                state(guest, "spinning")
                                second = state(guest, "idle")
                                require(second[1] == 2 and second[0] == settled[0] + points(second[3:]), "second award")
                                keys.tap("spc")
                                state(guest, "spinning")
                                keys.tap("esc")
                                result = guest.receive()
                                require("Slots closed." in result and "Spins=3" in result, "exit during spin")
                                require(memory_fields(guest.ask(b"mem\n", "PMM total=")) == original, "slots leaked pages")
                                guest.ask(b"echo back\n", "\nback\n")
                                serial_connection.sendall(b"slots\n")
                                reopened = state(guest, "idle")
                                require(reopened[1] == 3 and reopened[0] == second[0] + points(reopened[3:]), "resume/one settlement")
                                serial_connection.sendall(b"r")
                                require(state(guest, "idle") == (0, 0, 0, 0, 1, 3), "reset")
                                require(ready == monitor.snapshot(base.with_suffix(".ppm")), "reset image differs")
                                serial_connection.sendall(b"q")
                                guest.receive()
                                require(visible(monitor.snapshot(base.with_suffix(".ppm")), "malospal>"), "console restore")
                    else:
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: visible(frame, "malospal>"))
                        keys.text("slots\r")
                        ready = wait_picture(monitor, process, base.with_suffix(".ppm"), slot_visible)
                        time.sleep(0.2)
                        keys.tap("spc")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: reels(frame) != reels(ready))
                        time.sleep(2.0)
                        idle = monitor.snapshot(base.with_suffix(".ppm"))
                        time.sleep(0.3)
                        require(idle == monitor.snapshot(base.with_suffix(".ppm")), "no-COM1 reel settlement")
                        monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                        keys.tap("esc")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: visible(frame, "malospal>"))
                        keys.text("echo Malospal\r")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: visible(frame, "Malospal"))
                finally:
                    monitor.stream.close()
        finally:
            stop_vm(process)
    print(f"PASS: slots {kind}, {mode.upper()}, {memory} MiB; {base}", flush=True)
