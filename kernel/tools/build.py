"""Build and boot a freestanding x86-64 kernel on Windows or Linux."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".cache"
LIMINE = CACHE / "limine"
LIMINE_TAG = "v8.7.0-binary"
LIMINE_COMMIT = "aad3edd370955449717a334f0289dee10e2c5f01"
EXCEPTIONS = {
    "divide": (0, 0, 0),
    "invalid-opcode": (6, 0, 6),
    "general-protection": (13, 0x28, 13),
    "page-fault": (14, 0, 14),
    "double-fault": (8, 0, 8),
    "write-protect": (14, 3, 100),
    "no-execute": (14, 17, 101),
    "stack-guard": (14, 0, 102),
    "unmapped": (14, 0, 103),
    "text-write": (14, 3, 104),
    "stack-overflow": (8, 0, 105),
}


def run(command, **kwargs):
    return subprocess.run([str(arg) for arg in command], check=True, **kwargs)


def find_tool(name, variable, candidates=()):
    configured = os.environ.get(variable)
    if configured:
        resolved = shutil.which(configured)
        if resolved:
            return Path(resolved)
        if Path(configured).is_file():
            return Path(configured).resolve()
        raise RuntimeError(f"{variable} points to a missing executable: {configured}")
    installed = shutil.which(name)
    if installed:
        return Path(installed)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Missing {name}. Install it or set {variable}; see kernel/README.md.")


def prepare_limine():
    git = find_tool("git", "MALOSPAL_GIT")
    CACHE.mkdir(parents=True, exist_ok=True)
    if not LIMINE.exists():
        run([git, "clone", "--depth", "1", "--branch", LIMINE_TAG,
             "https://github.com/limine-bootloader/limine.git", LIMINE])
    revision = run([git, "-C", LIMINE, "rev-parse", "HEAD"],
                   capture_output=True, text=True).stdout.strip()
    if revision != LIMINE_COMMIT:
        raise RuntimeError("Unexpected Limine revision. Keep the pinned bootloader version.")
    run([git, "-C", LIMINE, "diff", "--quiet", "HEAD"])


def build_kernel(smoke=False, fault=None, memory_test=False, virtual_test=False, timer_test=False):
    prepare_limine()
    zig = find_tool("zig", "MALOSPAL_ZIG", [CACHE / "zig-x86_64-windows-0.15.2/zig.exe"])
    variant = (f"fault-{fault}" if fault else "timer" if timer_test else "virtual" if virtual_test else
               "memory" if memory_test else "smoke" if smoke else "normal")
    output = ROOT / "build" / variant
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = str(CACHE / "zig-global")
    env["ZIG_LOCAL_CACHE_DIR"] = str(CACHE / "zig-local")
    flags = ["-target", "x86_64-freestanding-none", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone",
             "-mcmodel=kernel", "-mno-mmx", "-mno-sse", "-mno-sse2",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-O2", "-g", "-fdebug-compilation-dir=.",
             f"-ffile-prefix-map={ROOT.parent}=.",
             f"-ffile-prefix-map={ROOT.parent.as_posix()}=.",
             f"-ffile-prefix-map={zig.parent}=toolchain/zig",
             "-I", str(ROOT / "include"), "-I", str(LIMINE)]
    if smoke:
        flags += ["-DMALOSPAL_SMOKE_TEST=1"]
    if fault:
        flags += [f"-DMALOSPAL_FAULT_TEST={EXCEPTIONS[fault][2]}"]
    if memory_test:
        flags += ["-DMALOSPAL_MEMORY_TEST=1"]
    if virtual_test:
        flags += ["-DMALOSPAL_VIRTUAL_TEST=1"]
    if timer_test:
        flags += ["-DMALOSPAL_TIMER_TEST=1"]
    objects = []
    sources = sorted((ROOT / "src").rglob("*.c")) + sorted((ROOT / "src").rglob("*.S"))
    sources.append(ROOT / "vendor/pcg/pcg_basic.c")
    if fault:
        sources.append(ROOT / "tests/fault.S")
        if EXCEPTIONS[fault][2] >= 100:
            sources.append(ROOT / "tests/vmm_fault.c")
    if memory_test:
        sources.append(ROOT / "tests/pmm_kernel.c")
    if virtual_test:
        sources.append(ROOT / "tests/vmm_kernel.c")
    if timer_test:
        sources += [ROOT / "tests/timer_kernel.c", ROOT / "tests/timer_registers.S"]
    for source in sources:
        obj = output / source.relative_to(ROOT).with_suffix(source.suffix + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        extra = ["-std=c17", "-Wall", "-Wextra", "-Werror"] if source.suffix == ".c" else []
        run([zig, "cc", *flags, *extra, "-c", source.relative_to(ROOT.parent).as_posix(),
             "-o", obj], env=env, cwd=ROOT.parent)
        objects.append(obj)
    elf = output / "kernel.elf"
    run([zig, "cc", "-target", "x86_64-freestanding-none", "-nostdlib", "-static",
         "-fno-pie", f"-Wl,-T,{ROOT / 'linker.ld'}", "-Wl,--build-id=none",
         "-Wl,-z,max-page-size=0x1000", *objects, "-o", elf], env=env)
    print(f"Kernel: {elf}", flush=True)
    return output


def check_pmm_host():
    zig = find_tool("zig", "MALOSPAL_ZIG", [CACHE / "zig-x86_64-windows-0.15.2/zig.exe"])
    output = ROOT / "build" / "host-tests"
    output.mkdir(parents=True, exist_ok=True)
    binary = output / ("pmm-test.exe" if os.name == "nt" else "pmm-test")
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = str(CACHE / "zig-global")
    env["ZIG_LOCAL_CACHE_DIR"] = str(CACHE / "zig-local")
    run([zig, "cc", "-std=c17", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=undefined", "-fsanitize-trap=undefined", "-I", ROOT / "include",
         ROOT / "src/mm/pmm.c", ROOT / "tests/pmm_host.c", "-o", binary], env=env)
    for case in ("basic", "sparse", "unaligned", "metadata", "invalid", "fragmented", "mixed"):
        run([binary, case], timeout=30)
    print("PASS: 7 PMM host test groups.", flush=True)


def check_pic_host():
    zig = find_tool("zig", "MALOSPAL_ZIG", [CACHE / "zig-x86_64-windows-0.15.2/zig.exe"])
    output = ROOT / "build" / "host-tests"
    output.mkdir(parents=True, exist_ok=True)
    binary = output / ("pic-test.exe" if os.name == "nt" else "pic-test")
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = str(CACHE / "zig-global")
    env["ZIG_LOCAL_CACHE_DIR"] = str(CACHE / "zig-local")
    run([zig, "cc", "-std=c17", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=undefined", "-fsanitize-trap=undefined", "-DMALOSPAL_IO_TEST=1",
         ROOT / "src/arch/x86_64/pic.c", ROOT / "tests/pic_host.c", "-o", binary], env=env)
    run([binary], timeout=30)


def check_console_host():
    zig = find_tool("zig", "MALOSPAL_ZIG", [CACHE / "zig-x86_64-windows-0.15.2/zig.exe"])
    output = ROOT / "build" / "host-tests"
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = str(CACHE / "zig-global")
    env["ZIG_LOCAL_CACHE_DIR"] = str(CACHE / "zig-local")
    for name, sources in (
        ("line", ["src/core/line.c", "tests/line_host.c"]),
        ("serial", ["src/arch/x86_64/serial.c", "src/core/debug.c", "tests/serial_host.c"]),
        ("screen", ["src/video/screen.c", "src/boot/framebuffer.c", "tests/screen_host.c"]),
        ("ps2-decode", ["src/arch/x86_64/ps2_decode.c", "tests/ps2_decode_host.c"]),
        ("usb-hid", ["src/usb/hid.c", "tests/hid_host.c"]),
        ("keyboard", ["src/arch/x86_64/keyboard.c", "src/arch/x86_64/ps2_decode.c", "tests/keyboard_host.c"]),
        ("snake", ["src/games/snake_logic.c", "tests/snake_host.c"]),
        ("slots", ["src/games/slots_logic.c", "vendor/pcg/pcg_basic.c", "tests/slots_host.c"]),
    ):
        binary = output / (f"{name}-test.exe" if os.name == "nt" else f"{name}-test")
        run([zig, "cc", "-std=c17", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=undefined", "-fsanitize-trap=undefined", "-DMALOSPAL_IO_TEST=1",
             "-I", ROOT / "include", *[ROOT / source for source in sources], "-o", binary], env=env)
        cases = (["24"], ["32"]) if name == "screen" else ([],)
        if name == "keyboard":
            cases = [[case] for case in ("normal", "absent", "busy", "selftest", "interface", "ack",
                                        "resend", "scanset", "config", "timeout", "parity", "ec", "slow-ec", "ec-timeout")]
        for arguments in cases:
            run([binary, *arguments], timeout=30)


def create_iso(output):
    sys.path.insert(0, str(CACHE / "python"))
    try:
        import pycdlib
    except ImportError as error:
        raise RuntimeError("Install ISO support: python -m pip install -r kernel/requirements.txt") from error

    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, joliet=3, rock_ridge="1.09", vol_ident="MALOSPAL_OS")
    files = [
        (output / "kernel.elf", "KERNEL.ELF", "kernel.elf"),
        (ROOT / "limine.conf", "LIMINE.CONF", "limine.conf"),
        (LIMINE / "limine-bios.sys", "LIMINE_BIOS.SYS", "limine-bios.sys"),
        (LIMINE / "limine-bios-cd.bin", "BIOS.BIN", "limine-bios-cd.bin"),
        (LIMINE / "limine-uefi-cd.bin", "UEFI.BIN", "limine-uefi-cd.bin"),
        (LIMINE / "LICENSE", "LICENSE.TXT", "limine-license.txt"),
        (ROOT / "vendor/pcg/LICENSE.txt", "PCG_LIC.TXT", "pcg-license.txt"),
        (ROOT / "vendor/pcg/README.md", "PCG_INFO.TXT", "pcg-info.txt"),
        (ROOT.parent / "AUTHORS.md", "AUTHORS.TXT", "authors.txt"),
        (ROOT.parent / "THIRD_PARTY.md", "NOTICES.TXT", "third-party.txt"),
    ]
    try:
        for source, iso_name, name in files:
            iso.add_file(str(source), iso_path=f"/{iso_name};1",
                         rr_name=name, joliet_path=f"/{name}")
        iso.add_eltorito("/BIOS.BIN;1", boot_load_size=4, boot_info_table=True)
        iso.add_eltorito("/UEFI.BIN;1", platform_id=0xef, efi=True)
        path = output / "malospal-os.iso"
        iso.write(str(path))
    finally:
        iso.close()
    print(f"Boot image: {path}", flush=True)
    if output.name == "normal":
        tree = output / "usb-uefi"
        payload = (
            (output / "kernel.elf", "kernel.elf"),
            (ROOT / "limine.conf", "limine.conf"),
            (LIMINE / "BOOTX64.EFI", "EFI/BOOT/BOOTX64.EFI"),
            (LIMINE / "LICENSE", "limine-license.txt"),
            (ROOT / "vendor/pcg/LICENSE.txt", "pcg-license.txt"),
            (ROOT / "vendor/pcg/README.md", "pcg-info.txt"),
            (ROOT.parent / "AUTHORS.md", "authors.txt"),
            (ROOT.parent / "THIRD_PARTY.md", "third-party.txt"),
        )
        manifest = []
        for source, relative in payload:
            destination = tree / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            manifest.append(f"{hashlib.sha256(destination.read_bytes()).hexdigest()}  {relative}\n")
        (tree / "SHA256SUMS").write_text("".join(manifest), encoding="ascii")
        print(f"UEFI USB files (copy to FAT32): {tree}", flush=True)
    return path


def qemu_path(path):
    path = Path(path).absolute()
    if os.name != "nt" or str(path).isascii():
        return str(path)
    # Some Windows QEMU builds cannot open Unicode filenames directly.
    import ctypes
    buffer = ctypes.create_unicode_buffer(32768)
    length = ctypes.windll.kernel32.GetShortPathNameW(str(path.parent), buffer, len(buffer))
    short = str(Path(buffer.value) / path.name)
    if not length or length >= len(buffer) or not short.isascii():
        raise RuntimeError("Windows QEMU needs an ASCII project path; use e.g. C:/Projects/Malospal-OS.")
    return short


def qemu_command(iso, memory, smoke=False, firmware=None, firmware_vars=None, uefi=False):
    qemu = find_tool("qemu-system-x86_64", "MALOSPAL_QEMU",
                     [CACHE / "qemu/qemu-system-x86_64.exe"])
    command = [qemu, "-machine", "q35", "-accel", "tcg", "-cpu", "qemu64",
               "-m", str(memory), "-smp", "1", "-cdrom", qemu_path(iso), "-boot", "d",
               "-display", "none", "-serial", "stdio", "-monitor", "none",
               "-nic", "none", "-no-reboot"]
    if firmware or uefi:
        code = (firmware or qemu.parent / "share/edk2-x86_64-code.fd").resolve()
        variables = (firmware_vars or code.parent / "edk2-i386-vars.fd").resolve()
        for path in (code, variables):
            if not path.is_file():
                raise RuntimeError(f"Missing UEFI file: {path}. Set --firmware and --firmware-vars.")
        code_path = qemu_path(code).replace(",", ",,")
        variables_path = qemu_path(variables).replace(",", ",,")
        # QEMU writes variable changes to a temporary overlay, not the firmware template.
        command += ["-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={code_path}",
                    "-drive", f"if=pflash,unit=1,format=raw,snapshot=on,file={variables_path}"]
    if smoke:
        command += ["-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"]
    return [str(arg) for arg in command]


def stop_vm(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def print_guest_log(text):
    start = text.find("\nMalospal OS ")
    print(text[start + 1:] if start >= 0 else text)


def check_boot(iso, memory, smoke, firmware=None, firmware_vars=None, uefi=False,
               memory_test=False, virtual_test=False, timer_test=False):
    mode = "uefi" if firmware or uefi else "bios"
    prefix = "timer" if timer_test else "virtual" if virtual_test else "memory" if memory_test else "boot"
    log = iso.parent / f"{prefix}-{mode}-{memory}.log"
    command = qemu_command(iso, memory, smoke, firmware, firmware_vars, uefi)
    with log.open("wb") as stream:
        if smoke:
            try:
                result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=60)
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(f"Boot timed out. See {log}") from error
            text = log.read_text(encoding="utf-8", errors="replace")
            # isa-debug-exit encodes the value written to it as (value << 1) | 1.
            success = result.returncode == 33 and "MALOSPAL_KERNEL_OK" in text and "[panic]" not in text
        else:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT)
            success = False
            try:
                deadline = time.monotonic() + 60
                requested = 0
                request_at = 0
                while time.monotonic() < deadline:
                    text = log.read_text(encoding="utf-8", errors="replace")
                    if process.poll() is not None or "[panic]" in text:
                        break
                    ticks = re.findall(r"^TIMER ticks=(\d+) nominal_hz=100$", text, re.MULTILINE)
                    if len(ticks) >= 2 and int(ticks[1]) > int(ticks[0]) and text.endswith("malospal> "):
                        success = True
                        break
                    if (text.endswith("malospal> ") and len(ticks) == requested and requested < 2
                            and time.monotonic() >= request_at):
                        # Windows QEMU's stdio pipe backend can drop a burst
                        # before the emulated UART accepts its next byte.
                        for value in b"ticks\n":
                            process.stdin.write(bytes([value]))
                            process.stdin.flush()
                            time.sleep(0.02)
                        requested += 1
                        request_at = time.monotonic() + 0.2
                    time.sleep(0.1)
            finally:
                stop_vm(process)
                process.stdin.close()
    text = log.read_text(encoding="utf-8", errors="replace")
    print_guest_log(text)
    success = success and "[ok] Own GDT, TSS and IDT; exception handlers ready" in text
    if not smoke:
        success = success and "[ok] Serial input ready (COM1 IRQ4)" in text and "MALOSPAL_KERNEL_OK" in text
    tables = re.search(r"^VMM tables=(\d+) root=0x([0-9a-f]+)$", text, re.MULTILINE)
    success = success and "[ok] Own page tables active;" in text and tables is not None
    counters = re.search(r"^PMM total=(\d+) free=(\d+) allocated=(\d+) metadata=(\d+)$", text, re.MULTILINE)
    if counters:
        total, free, allocated, metadata = map(int, counters.groups())
        success = (success and total == free + allocated + metadata and free > 0 and metadata > 0
                   and allocated == int(tables.group(1)) and allocated > 0 and int(tables.group(2), 16) % 4096 == 0)
    else:
        success = False
    if memory_test:
        restored = re.search(r"PMM exhausted and restored (\d+) pages: OK", text)
        success = (success and "MALOSPAL_PMM_TEST_OK" in text and restored is not None
                   and int(restored.group(1)) == free)
    if virtual_test:
        success = success and "MALOSPAL_VMM_TEST_OK" in text
    if not (memory_test or virtual_test):
        success = success and "[ok] PIC IRQs and PIT timer ready (~100 Hz)" in text
        if smoke and not timer_test:
            success = success and "MALOSPAL_TIMER_RUNNING" in text
    if timer_test:
        ticks = re.search(r"^TIMER ticks=(\d+) waited=(\d+) frames=(\d+)$", text, re.MULTILINE)
        success = (success and "MALOSPAL_TIMER_TEST_OK" in text and ticks is not None
                   and int(ticks.group(1)) >= 119 and int(ticks.group(2)) >= 103
                   and int(ticks.group(3)) >= 16)
    if not success:
        raise RuntimeError(f"Boot check failed. See {log}")
    kind = ("timer test" if timer_test else "virtual memory test" if virtual_test else
            "memory test" if memory_test else "test kernel" if smoke else "normal kernel")
    print(f"PASS: {kind}, {mode.upper()}, {memory} MiB. Log: {log}", flush=True)


def check_exception(iso, fault, memory, firmware=None, firmware_vars=None, uefi=False):
    mode = "uefi" if firmware or uefi else "bios"
    log = iso.parent / f"exception-{mode}-{memory}.log"
    command = qemu_command(iso, memory, True, firmware, firmware_vars, uefi)
    with log.open("wb") as stream:
        try:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=60)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"Exception test timed out. See {log}") from error
    text = log.read_text(encoding="utf-8", errors="replace")
    print_guest_log(text)
    vector, error_code, test_id = EXCEPTIONS[fault]
    fields = {name: int(value, 16) for name, value in
              re.findall(r"\b(error|rip|cs|rsp|cr2|r15)=0x([0-9a-f]+)", text)}
    record = re.search(r"^vector=(\d+) name=", text, re.MULTILINE)
    site = re.search(r"Expected fault RIP=0x([0-9a-f]+)", text)
    success = (
        result.returncode == 35 and "MALOSPAL_EXCEPTION_HALTED" in text
        and record is not None and int(record.group(1)) == vector
        and fields.get("error") == error_code and fields.get("cs") == 8
        and fields.get("r15") == 0x123456789abcdef0
        and "[ok] Own page tables active;" in text
    )
    if vector == 8:
        success = success and "handler_stack=double-fault-IST" in text
    else:
        success = success and site is not None and fields.get("rip") == int(site.group(1), 16)
    if fault == "page-fault":
        success = success and fields.get("cr2") == 0x0000400000000000
    if test_id >= 100:
        address = re.search(r"Expected fault address=0x([0-9a-f]+)", text)
        success = success and address is not None and fields.get("cr2") == int(address.group(1), 16)
    if not success:
        raise RuntimeError(f"Exception diagnostics did not match {fault}. See {log}")
    print(f"PASS: {fault}, {mode.upper()}, vector {vector}, error {error_code}. Log: {log}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "run", "smoke", "check", "fault", "memory", "virtual", "timer", "console", "screen", "keyboard", "usb", "game", "slots", "unit"])
    parser.add_argument("--exception", choices=EXCEPTIONS, help="Required with the fault action")
    parser.add_argument("--memory", type=int, default=128, help="Guest RAM in MiB")
    parser.add_argument("--headless", action="store_true", help="Hide the display for interactive run")
    boot_mode = parser.add_mutually_exclusive_group()
    boot_mode.add_argument("--uefi", action="store_true", help="Use QEMU's bundled EDK2 firmware")
    boot_mode.add_argument("--firmware", type=Path, help="OVMF/EDK2 CODE image for UEFI boot")
    parser.add_argument("--firmware-vars", type=Path, help="Matching OVMF/EDK2 VARS template")
    args = parser.parse_args()
    if args.memory < 32:
        parser.error("Use at least 32 MiB of guest RAM")
    if args.firmware_vars and not (args.firmware or args.uefi):
        parser.error("--firmware-vars requires --firmware or --uefi")
    if (args.action == "fault") != (args.exception is not None):
        parser.error("Use --exception with the fault action only")
    if args.action == "unit":
        check_pmm_host()
        check_pic_host()
        check_console_host()
        return
    if args.action == "check":
        check_pmm_host()
        check_pic_host()
        check_console_host()
        normal_iso = create_iso(build_kernel(False))
        smoke_iso = create_iso(build_kernel(True))
        check_boot(smoke_iso, 64, True)
        check_boot(smoke_iso, 256, True, args.firmware, args.firmware_vars, True)
        check_boot(normal_iso, 128, False)
        check_boot(normal_iso, 256, False, args.firmware, args.firmware_vars, True)
        for fault in EXCEPTIONS:
            iso = create_iso(build_kernel(fault=fault))
            check_exception(iso, fault, 128)
            check_exception(iso, fault, 256, args.firmware, args.firmware_vars, True)
        memory_iso = create_iso(build_kernel(memory_test=True))
        check_boot(memory_iso, 32, True, memory_test=True)
        check_boot(memory_iso, 128, True, memory_test=True)
        check_boot(memory_iso, 256, True, args.firmware, args.firmware_vars, True, memory_test=True)
        virtual_iso = create_iso(build_kernel(virtual_test=True))
        check_boot(virtual_iso, 32, True, virtual_test=True)
        check_boot(virtual_iso, 128, True, virtual_test=True)
        check_boot(virtual_iso, 256, True, args.firmware, args.firmware_vars, True, virtual_test=True)
        timer_iso = create_iso(build_kernel(timer_test=True))
        check_boot(timer_iso, 32, True, timer_test=True)
        check_boot(timer_iso, 128, True, timer_test=True)
        check_boot(timer_iso, 256, True, args.firmware, args.firmware_vars, True, timer_test=True)
        from console_test import check_console
        check_console(normal_iso, 32)
        check_console(normal_iso, 128)
        check_console(normal_iso, 256, args.firmware, args.firmware_vars, True)
        from screen_test import check_screen
        check_screen(normal_iso, 128)
        check_screen(normal_iso, 256, args.firmware, args.firmware_vars, True)
        check_screen(normal_iso, 128, serial=False)
        check_screen(normal_iso, 256, args.firmware, args.firmware_vars, True, serial=False)
        check_screen(normal_iso, 128, video=False)
        check_screen(normal_iso, 128, serial=False, keyboard=False)
        from keyboard_test import check_keyboard
        check_keyboard(normal_iso, 128)
        check_keyboard(normal_iso, 256, args.firmware, args.firmware_vars, True)
        check_keyboard(normal_iso, 128, serial=False)
        check_keyboard(normal_iso, 256, args.firmware, args.firmware_vars, True, serial=False)
        check_keyboard(normal_iso, 128, keyboard=False)
        from game_test import check_game
        check_game(normal_iso, 128)
        check_game(normal_iso, 256, args.firmware, args.firmware_vars, True)
        check_game(normal_iso, 128, serial=False)
        check_game(normal_iso, 128, video=False)
        from slots_test import check_slots
        check_slots(normal_iso, 128)
        check_slots(normal_iso, 256, args.firmware, args.firmware_vars, True)
        check_slots(normal_iso, 128, serial=False)
        check_slots(normal_iso, 128, video=False)
        from usb_test import check_usb, check_usb_topology
        check_usb(normal_iso, 128)
        check_usb(normal_iso, 256, args.firmware, args.firmware_vars, True)
        check_usb(normal_iso, 128, serial=False)
        check_usb(normal_iso, 256, args.firmware, args.firmware_vars, True, serial=False)
        check_usb_topology(normal_iso)
        check_usb_topology(normal_iso, mixed=True)
        print("PASS: 30 host groups, 4 boot, 22 exception, 3 PMM, 3 VMM, 3 timer, 3 console, 6 screen, 5 keyboard, 4 Snake, 4 slots and 6 USB checks (63 QEMU runs).")
        return
    if args.action == "slots":
        from slots_test import check_slots
        iso = create_iso(build_kernel())
        check_slots(iso, args.memory, args.firmware, args.firmware_vars, args.uefi)
        check_slots(iso, args.memory, args.firmware, args.firmware_vars, args.uefi, serial=False)
        return
    if args.action == "game":
        from game_test import check_game
        iso = create_iso(build_kernel())
        check_game(iso, args.memory, args.firmware, args.firmware_vars, args.uefi)
        check_game(iso, args.memory, args.firmware, args.firmware_vars, args.uefi, serial=False)
        return
    if args.action == "usb":
        from usb_test import check_usb
        iso = create_iso(build_kernel())
        check_usb(iso, args.memory, args.firmware, args.firmware_vars, args.uefi)
        check_usb(iso, args.memory, args.firmware, args.firmware_vars, args.uefi, serial=False)
        return
    if args.action == "keyboard":
        from keyboard_test import check_keyboard
        iso = create_iso(build_kernel())
        check_keyboard(iso, args.memory, args.firmware, args.firmware_vars, args.uefi)
        check_keyboard(iso, args.memory, args.firmware, args.firmware_vars, args.uefi, serial=False)
        return
    if args.action == "screen":
        from screen_test import check_screen
        iso = create_iso(build_kernel())
        check_screen(iso, args.memory, args.firmware, args.firmware_vars, args.uefi)
        check_screen(iso, args.memory, args.firmware, args.firmware_vars, args.uefi, serial=False)
        return
    if args.action == "console":
        from console_test import check_console
        iso = create_iso(build_kernel())
        check_console(iso, args.memory, args.firmware, args.firmware_vars, args.uefi)
        return
    if args.action == "timer":
        iso = create_iso(build_kernel(timer_test=True))
        check_boot(iso, args.memory, True, args.firmware, args.firmware_vars, args.uefi, timer_test=True)
        return
    if args.action == "virtual":
        iso = create_iso(build_kernel(virtual_test=True))
        check_boot(iso, args.memory, True, args.firmware, args.firmware_vars, args.uefi, virtual_test=True)
        return
    if args.action == "memory":
        iso = create_iso(build_kernel(memory_test=True))
        check_boot(iso, args.memory, True, args.firmware, args.firmware_vars, args.uefi, memory_test=True)
        return
    if args.action == "fault":
        iso = create_iso(build_kernel(fault=args.exception))
        check_exception(iso, args.exception, args.memory, args.firmware, args.firmware_vars, args.uefi)
        return
    smoke = args.action == "smoke"
    output = build_kernel(smoke)
    iso = create_iso(output)
    if args.action == "build":
        return
    if smoke:
        check_boot(iso, args.memory, True, args.firmware, args.firmware_vars, args.uefi)
        return
    command = qemu_command(iso, args.memory, False, args.firmware, args.firmware_vars, args.uefi)
    if not smoke:
        if not args.headless:
            index = command.index("-display")
            del command[index:index + 2]
        print("Starting Malospal OS. Press Ctrl+C to stop the emulator.", flush=True)
        print("Type in the VM window (PS/2, US layout) or this terminal (COM1).", flush=True)
        if os.name == "nt" and sys.stdin.isatty():
            from terminal import run_windows_terminal
            run_windows_terminal(command)
            return
        process = subprocess.Popen(command)
        try:
            status = process.wait()
            if status != 0:
                raise RuntimeError(f"QEMU exited with status {status}")
        except KeyboardInterrupt:
            pass
        finally:
            stop_vm(process)
        return


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
