"""Real xHCI/HID transfers in QEMU, with the i8042 controller absent."""
import re
import subprocess
import time

from build import qemu_command, stop_vm
from console_test import Console, require
from screen_test import Monitor, listener
from keyboard_test import Keyboard, wait_picture, visible
from game_test import game_visible
from slots_test import slot_visible, reels, state


class USBKeyboard(Keyboard):
    def key(self, code, down):
        super().key(code, down)
        # QEMU's boot keyboard has a 10 ms interrupt interval. Give the actual
        # USB device time to deliver each report, including modifier changes.
        time.sleep(0.015)


def check_usb(iso, memory=128, firmware=None, firmware_vars=None, uefi=False, serial=True):
    mode = "uefi" if firmware or uefi else "bios"
    base = iso.parent / f"usb-{mode}-{memory}-{'serial' if serial else 'screen'}"
    with listener() as qmp, listener() as uart, base.with_suffix(".log").open("wb") as stream, \
            base.with_suffix(".qemu.log").open("wb") as errors:
        command = qemu_command(iso, memory, False, firmware, firmware_vars, uefi)
        command[command.index("-machine") + 1] = "q35,i8042=off"
        command[command.index("-serial") + 1] = f"tcp:127.0.0.1:{uart.getsockname()[1]}" if serial else "none"
        command += ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,id=usbkeys,bus=xhci.0",
                    "-qmp", f"tcp:127.0.0.1:{qmp.getsockname()[1]}"]
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=errors, stderr=subprocess.STDOUT)
        try:
            connection, _ = qmp.accept()
            with connection:
                monitor = Monitor(connection)
                try:
                    keys = USBKeyboard(monitor)
                    if serial:
                        channel, _ = uart.accept()
                        with channel:
                            channel.settimeout(0.2)
                            guest = Console(channel, process, stream)
                            guest.deadline = time.monotonic() + 180
                            boot = guest.receive()
                            require("[ok] USB keyboard input ready" in boot, f"USB init: {boot}")
                            require("[warn] PS/2 keyboard unavailable" in boot, "PS/2 must be absent")

                            def ask(command, expected):
                                keys.text(command + "\r")
                                result = guest.receive()
                                require(expected in result, f"USB {command!r}: {result!r}")
                                return result

                            ask("info", "Input: USB xHCI (US)")
                            before = ask("mem", "PMM total=")
                            first = re.search(r"ticks=(\d+)", ask("ticks", "TIMER ticks="))[1]
                            ask("helx\bp", "Command list")
                            ask("echo ABC xyz 0123456789 ~!@#$%^&*()_+{}|:\"<>?", "\nABC xyz 0123456789 ~!@#$%^&*()_+{}|:\"<>?\n")
                            keys.text("halt")
                            keys.clear(right=True)
                            ask("echo cancelled", "\ncancelled\n")
                            keys.text("echo ")
                            keys.tap("caps_lock")
                            keys.text("ab")
                            keys.key("shift_r", True)
                            keys.tap("c")
                            keys.key("shift_r", False)
                            keys.tap("caps_lock")
                            ask("", "\nABc\n")
                            for _ in range(30):
                                ask("echo ring-wrap", "\nring-wrap\n")
                            second = re.search(r"ticks=(\d+)", ask("ticks", "TIMER ticks="))[1]
                            require(int(second) > int(first), "USB stalled PIT")
                            after = ask("mem", "PMM total=")
                            require(before == after, "USB typing leaked pages")
                            counters = ask("keyboard", "USB controllers=1 keyboards=1")
                            require("errors=0 unsupported=0 failed=0" in counters, "USB errors")
                            keys.text("snake\r")
                            guest.receive(marker=b"SNAKE READY score=0000\r\n")
                            require(game_visible(monitor.snapshot(base.with_suffix(".ppm"))), "USB Snake screen")
                            keys.tap("spc")
                            guest.receive(marker=b"SNAKE PLAYING score=0000\r\n")
                            keys.tap("up")
                            time.sleep(0.2)
                            keys.tap("p")
                            guest.receive(marker=b"SNAKE PAUSED score=")
                            paused = monitor.snapshot(base.with_suffix(".ppm"))
                            time.sleep(0.15)
                            require(paused == monitor.snapshot(base.with_suffix(".ppm")), "USB Snake pause")
                            keys.tap("esc")
                            require("Snake closed." in guest.receive(), "USB Snake exit")
                            keys.text("slots\r")
                            state(guest, "idle")
                            ready = monitor.snapshot(base.with_suffix(".ppm"))
                            require(slot_visible(ready), "USB slots screen")
                            keys.tap("spc")
                            state(guest, "spinning")
                            require(state(guest, "idle")[1] == 1, "USB slot spin")
                            keys.tap("esc")
                            require("Slots closed." in guest.receive(), "USB slots exit")
                            require(ask("mem", "PMM total=") == before, "USB games leaked pages")
                            guest.connection.sendall(b"ha")
                            guest.receive(marker=b"ha")
                            ask("lt", "Input source changed; current line discarded")
                            # Removal cancels an incomplete command and keeps serial usable.
                            keys.text("ha")
                            guest.receive(marker=b"ha")
                            monitor.execute("device_del", id="usbkeys")
                            guest.receive(marker=b"current line discarded\r\n")
                            guest.ask(b"lt\r", "Line discarded")
                            guest.ask(b"info\r", "Input: USB unavailable")
                            guest.ask(b"echo fallback\r", "\nfallback\n")
                    else:
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda f: visible(f, "malospal>"))
                        keys.text("echo Malospal\r")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda f: visible(f, "Malospal"))
                        keys.text("slots\r")
                        ready = wait_picture(monitor, process, base.with_suffix(".ppm"), slot_visible)
                        keys.tap("spc")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda f: reels(f) != reels(ready))
                        keys.tap("esc")
                        wait_picture(monitor, process, base.with_suffix(".ppm"), lambda f: visible(f, "malospal>"))
                    monitor.execute("screendump", filename=str(base.with_suffix(".png")), format="png")
                finally:
                    monitor.stream.close()
        finally:
            stop_vm(process)
    print(f"PASS: USB xHCI, no PS/2, {mode.upper()}, {'serial' if serial else 'screen'}; {base}", flush=True)


def check_usb_topology(iso, mixed=False):
    """No keyboard, or unsupported devices alongside a usable keyboard on another host."""
    base = iso.parent / f"usb-topology-{'mixed' if mixed else 'empty'}"
    with listener() as qmp, listener() as uart, base.with_suffix(".log").open("wb") as stream, \
            base.with_suffix(".qemu.log").open("wb") as errors:
        command = qemu_command(iso, 128)
        command[command.index("-machine") + 1] = "q35,i8042=off"
        command[command.index("-serial") + 1] = f"tcp:127.0.0.1:{uart.getsockname()[1]}"
        command += ["-qmp", f"tcp:127.0.0.1:{qmp.getsockname()[1]}", "-device", "qemu-xhci,id=first"]
        if mixed:
            command += ["-device", "usb-mouse,bus=first.0", "-device", "usb-hub,bus=first.0",
                        "-device", "qemu-xhci,id=second", "-device", "usb-kbd,bus=second.0"]
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=errors, stderr=subprocess.STDOUT)
        try:
            connection, _ = qmp.accept()
            with connection:
                monitor = Monitor(connection)
                try:
                    channel, _ = uart.accept()
                    with channel:
                        channel.settimeout(0.2)
                        guest = Console(channel, process, stream)
                        boot = guest.receive()
                        expected = "xHCI ready=2 keyboards=1 unsupported=2 failed=0" if mixed else "xHCI ready=1 keyboards=0 unsupported=0 failed=0"
                        require(expected in boot, f"USB topology: {boot}")
                        if mixed:
                            USBKeyboard(monitor).text("echo topology\r")
                            require("\ntopology\n" in guest.receive(), "USB second controller input")
                        else:
                            guest.ask(b"info\r", "Input: USB unavailable")
                            guest.ask(b"ticks\r", "TIMER ticks=")
                finally:
                    monitor.stream.close()
        finally:
            stop_vm(process)
    print(f"PASS: USB {'multiple controllers, mouse/hub skipped' if mixed else 'empty controller, serial fallback'}; {base}", flush=True)
