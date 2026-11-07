"""Play the normal kernel's game through PS/2 and verify actual framebuffer pixels."""

import subprocess
import time

from build import qemu_command, stop_vm
from console_test import Console, memory_fields, require
from keyboard_test import Keyboard, visible, wait_picture
from screen_test import Monitor, listener


def colored_pixels(frame, rgb):
    pixels = frame[2]
    target = bytes(rgb)
    return sum(pixels[i:i + 3] == target for i in range(0, len(pixels), 3))


def game_visible(frame):
    return colored_pixels(frame, (0xc4, 0xf4, 0x77)) > 100 and colored_pixels(frame, (0x4f, 0xd6, 0xa2)) > 100


def board(frame):
    width, height, pixels = frame
    cell = min((width - 48) // 24, (height - 128) // 18, 32)
    x = (width - cell * 24) // 2
    y = 96 + (height - 128 - cell * 18) // 2
    return b"".join(pixels[((y + row) * width + x) * 3:((y + row) * width + x + cell * 24) * 3]
                    for row in range(cell * 18))


def check_game(iso, memory, firmware=None, firmware_vars=None, uefi=False, serial=True, video=True):
    mode = "uefi" if firmware or uefi else "bios"
    kind = "normal" if serial and video else "no-serial" if video else "no-video"
    base = iso.parent / f"game-{mode}-{memory}-{kind}"
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
                                guest.ask(b"snake\n", "Snake needs a framebuffer")
                                guest.ask(b"ticks\n", "TIMER ticks=")
                            else:
                                keys.text("snake\r")
                                guest.receive(marker=b"SNAKE READY score=0000\r\n")
                                ready = monitor.snapshot(base.with_suffix(".ppm"))
                                require(game_visible(ready), "Snake head/body pixels")
                                require(colored_pixels(ready, (0xff, 0xb6, 0x5b)) > 100, "food/status pixels")
                                keys.tap("spc")
                                guest.receive(marker=b"SNAKE PLAYING score=0000\r\n")
                                guest.receive(marker=b"SNAKE PLAYING score=0001\r\n")
                                keys.tap("p")
                                guest.receive(marker=b"SNAKE PAUSED score=0001\r\n")
                                paused = monitor.snapshot(base.with_suffix(".ppm"))
                                require(board(paused) != board(ready), "Snake did not move/grow")
                                time.sleep(0.35)
                                require(monitor.snapshot(base.with_suffix(".ppm")) == paused, "pause did not freeze frame")
                                keys.tap("up")
                                keys.tap("spc")
                                guest.receive(marker=b"SNAKE PLAYING score=0001\r\n")
                                time.sleep(0.25)
                                keys.tap("p")
                                guest.receive(marker=b"SNAKE PAUSED score=0001\r\n")
                                turned = monitor.snapshot(base.with_suffix(".ppm"))
                                require(board(turned) != board(paused), "arrow movement did not change pixels")
                                monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                                keys.tap("r")
                                guest.receive(marker=b"SNAKE READY score=0000\r\n")
                                require(board(monitor.snapshot(base.with_suffix(".ppm"))) == board(ready), "restart board")
                                keys.tap("esc")
                                require("Snake closed. Score=0 Best=1" in guest.receive(), "exit/best score")
                                require(visible(monitor.snapshot(base.with_suffix(".ppm")), "malospal>"), "console not restored")
                                require(memory_fields(guest.ask(b"mem\n", "PMM total=")) == original, "game leaked pages")
                                guest.ask(b"ticks\n", "TIMER ticks=")
                                serial_connection.sendall(b"snake\n")
                                guest.receive(marker=b"SNAKE READY score=0000\r\n")
                                serial_connection.sendall(b"d")
                                guest.receive(marker=b"SNAKE PLAYING score=0000\r\n")
                                serial_connection.sendall(b"q")
                                require("Snake closed." in guest.receive(), "COM1 game controls")
                                guest.ask(b"echo back\n", "\nback\n")
                    else:
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: visible(frame, "malospal>"))
                        keys.text("snake\r")
                        ready = wait_picture(monitor, process, base.with_suffix(".ppm"), game_visible)
                        keys.tap("spc")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: board(frame) != board(ready))
                        keys.tap("p")
                        time.sleep(0.15)
                        paused = monitor.snapshot(base.with_suffix(".ppm"))
                        time.sleep(0.3)
                        require(paused == monitor.snapshot(base.with_suffix(".ppm")), "no-COM1 pause")
                        monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                        keys.tap("esc")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: visible(frame, "malospal>"))
                        keys.text("echo Malospal\r")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda frame: visible(frame, "Malospal"))
                finally:
                    monitor.stream.close()
        finally:
            stop_vm(process)
    print(f"PASS: game {kind}, {mode.upper()}, {memory} MiB; {base}", flush=True)
