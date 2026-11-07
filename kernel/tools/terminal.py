"""Windows console bridge for QEMU: keep pasted input out of its lossy stdio path."""

import select
import socket
import subprocess
import sys

from build import stop_vm


def run_windows_terminal(command):
    import msvcrt

    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(60)
        command = list(command)
        command[command.index("-serial") + 1] = f"tcp:127.0.0.1:{listener.getsockname()[1]}"
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL)
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(1)
                while process.poll() is None:
                    if select.select([connection], [], [], 0.01)[0]:
                        try:
                            output = connection.recv(4096)
                        except (ConnectionResetError, ConnectionAbortedError):
                            break
                        if not output:
                            break
                        sys.stdout.buffer.write(output)
                        sys.stdout.buffer.flush()
                    pending = bytearray()
                    for _ in range(64):
                        if not msvcrt.kbhit():
                            break
                        value = msvcrt.getwch()
                        if value == '\x03':
                            raise KeyboardInterrupt
                        if value in ('\x00', '\xe0'):
                            msvcrt.getwch()
                            pending.append(0x1b)  # Unsupported extended key; reject the line.
                        else:
                            pending.append(ord(value) if ord(value) < 128 else 0xff)
                    if pending:
                        connection.sendall(pending)
            # Closing the display can disconnect COM1 before the process exits.
            try:
                status = process.wait(timeout=5)
            except subprocess.TimeoutExpired as error:
                raise RuntimeError("QEMU closed the serial connection unexpectedly") from error
            if status != 0:
                raise RuntimeError(f"QEMU exited with status {status}")
        except KeyboardInterrupt:
            pass
        finally:
            stop_vm(process)
