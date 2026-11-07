"""Exercise the ordinary kernel through its COM1 port, without test commands."""

import re
import socket
import subprocess
import time

from build import qemu_command, stop_vm

PROMPT = b"malospal> "


def require(condition, message):
    if not condition:
        raise RuntimeError(f"Console check failed: {message}")


class Console:
    def __init__(self, connection, process, stream):
        self.connection = connection
        self.process = process
        self.stream = stream
        self.deadline = time.monotonic() + 60

    def receive(self, prompts=1, marker=None):
        data = bytearray()
        while time.monotonic() < self.deadline:
            require(self.process.poll() is None, "QEMU exited unexpectedly")
            try:
                part = self.connection.recv(4096)
            except socket.timeout:
                continue
            require(part, "serial connection closed")
            self.stream.write(part)
            self.stream.flush()
            data.extend(part)
            require(b"[panic]" not in data, "kernel panic")
            if marker is not None and marker in data:
                return bytes(data).replace(b"\r\n", b"\n").decode("ascii", errors="replace")
            if marker is None and data.count(PROMPT) >= prompts and data.endswith(PROMPT):
                require(data.count(PROMPT) == prompts, "unexpected extra prompt")
                return bytes(data).replace(b"\r\n", b"\n").decode("ascii", errors="replace")
        raise RuntimeError("Console check timed out")

    def ask(self, command, expected, prompts=1):
        self.connection.sendall(command)
        text = self.receive(prompts)
        require(expected in text, f"missing response {expected!r} for {command[:40]!r}")
        return text


def memory_fields(text):
    memory = re.search(r"^PMM total=(\d+) free=(\d+) allocated=(\d+) metadata=(\d+)$", text, re.M)
    tables = re.search(r"^VMM tables=(\d+) root=(0x[0-9a-f]+)$", text, re.M)
    require(memory is not None and tables is not None, "missing memory snapshot")
    total, free, allocated, metadata = map(int, memory.groups())
    require(total == free + allocated + metadata and free > 0 and metadata > 0, "PMM counters")
    require(allocated == int(tables[1]) and int(tables[2], 16) % 4096 == 0, "VMM counters/root")
    return memory.groups(), tables.groups()


def serial_fields(text):
    fields = re.search(r"SERIAL received=(\d+) queued=(\d+) dropped=(\d+) errors=(\d+) irqs=(\d+) faulted=(\d+)", text)
    require(fields is not None, "missing UART counters")
    received, queued, dropped, errors, irqs, faulted = map(int, fields.groups())
    require(received > 0 and irqs > 0 and queued == dropped == errors == faulted == 0, "UART counters/loss")
    return received, irqs


def check_console(iso, memory, firmware=None, firmware_vars=None, uefi=False):
    mode = "uefi" if firmware or uefi else "bios"
    log = iso.parent / f"console-{mode}-{memory}.log"
    diagnostics = iso.parent / f"console-{mode}-{memory}-qemu.log"
    # Bind first so there is no race over an ephemeral port. QEMU connects as
    # a client; no guest networking or externally accessible listener is used.
    with socket.socket() as listener, log.open("wb") as stream, diagnostics.open("wb") as errors:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(60)
        command = qemu_command(iso, memory, False, firmware, firmware_vars, uefi)
        command[command.index("-serial") + 1] = f"tcp:127.0.0.1:{listener.getsockname()[1]}"
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=errors, stderr=subprocess.STDOUT)
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(0.2)
                guest = Console(connection, process, stream)
                boot = guest.receive()
                require("MALOSPAL_KERNEL_OK" in boot and "[ok] Serial input ready (COM1 IRQ4)" in boot, "boot markers")
                original_memory = memory_fields(boot)
                help_text = guest.ask(b"help\r\n", "Command list")
                for name in ("help", "info", "mem", "ticks", "serial", "keyboard", "echo", "snake", "slots", "halt"):
                    require(re.search(rf"^{name}\s+", help_text, re.M) is not None, f"help entry {name}")
                guest.ask(b"info\n", "Input: COM1 IRQ4")
                require(memory_fields(guest.ask(b"mem\r", "PMM total=")) == original_memory, "memory snapshot changed")
                before = guest.ask(b"ticks\n", "TIMER ticks=")
                time.sleep(0.1)
                after = guest.ask(b"\tticks\t \r\n", "TIMER ticks=")
                first = re.search(r"TIMER ticks=(\d+)", before)
                second = re.search(r"TIMER ticks=(\d+)", after)
                require(first is not None and second is not None and int(second[1]) > int(first[1]), "timer stalled during console input")
                serial_fields(guest.ask(b"serial\n", "SERIAL received="))
                guest.ask(b"no-such-command\n", "[error] Unknown command:")
                guest.ask(b"halt argument\n", "[error] Unexpected arguments")
                guest.ask(b"HELP\n", "[error] Unknown command:")
                guest.ask(b"helx\bp\r", "Command list")
                guest.ask(b"memx\x7f\n", "PMM total=")
                guest.ask(b"halt\x15echo cancelled\n", "\ncancelled\n")
                guest.ask(b"\b\x7f\r\n", "\nmalospal> ")
                for invalid in (b"ha\x00lt\n", b"halt\x1b[A\n", b"ha\xfflt\n"):
                    guest.ask(invalid, "[error] Line discarded:")
                guest.ask(b"echo " + b"x" * 122 + b"\n", "\n" + "x" * 122 + "\n")
                guest.ask(b"echo " + b"x" * 123 + b"\b\n", "[error] Line discarded:")
                guest.ask(b"echo recovered\n", "\nrecovered\n")
                # Multiple complete commands in a paste; enough total input to
                # exercise the ring indices wrapping without intentionally losing bytes.
                batch = b"".join(f"echo b{i:02}\n".encode() for i in range(80))
                responses = guest.ask(batch, "\nb79\n", prompts=80)
                for i in range(80):
                    require(responses.count(f"\nb{i:02}\n") == 1, f"batch command {i}")
                received, irqs = serial_fields(guest.ask(b"serial\n", "SERIAL received="))
                require(received > 1024, "ring wrap was not exercised")
                require(memory_fields(guest.ask(b"mem\n", "PMM total=")) == original_memory, "console leaked memory")
                connection.sendall(b"halt\n")
                guest.receive(marker=b"Kernel halted.\r\n")
                connection.sendall(b"info\n")
                try:
                    unexpected = connection.recv(4096)
                except socket.timeout:
                    unexpected = b""
                require(not unexpected and process.poll() is None, "halt did not stop the guest kernel")
        finally:
            stop_vm(process)
    print(f"PASS: console, {mode.upper()}, {memory} MiB; commands, editing, recovery, paste, IRQ4 ({irqs} IRQs/{received} bytes), halt. Log: {log}", flush=True)
