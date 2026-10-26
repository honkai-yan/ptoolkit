"""POSIX PTY integration check. Run after building the app and editor demo.

Uses an in-memory clipboard in the editor demo. Never opens user configuration.
"""
import errno
import fcntl
import os
from pathlib import Path
import re
import select
import signal
import struct
import pty
import sys
import tempfile
import termios
import time

ANSI = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")


class Terminal:
    def __init__(self, args, env=None):
        gate_read, gate_write = os.pipe()
        self.pid, self.master = pty.fork()
        if self.pid == 0:
            os.close(gate_write)
            os.read(gate_read, 1)
            os.close(gate_read)
            environment = {**os.environ, "TERM": "xterm-256color"}
            if env:
                environment.update(env)
            os.execvpe(args[0], args, environment)
        os.close(gate_read)
        self.status = None
        self.output = b""
        self.original = termios.tcgetattr(self.master)
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
        os.write(gate_write, b"g")
        os.close(gate_write)

    def wait_text(self, text):
        deadline = time.monotonic() + 8
        while text not in ANSI.sub(b"", self.output):
            if time.monotonic() > deadline:
                raise AssertionError(f"Missing {text!r}; output tail: {self.output[-400:]!r}")
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        chunk = b""
                    else:
                        raise
                if not chunk:
                    raise AssertionError(f"Process exited before {text!r}: {self.output[-400:]!r}")
                self.output += chunk

    def send(self, value):
        os.write(self.master, value)

    def finish(self):
        deadline = time.monotonic() + 8
        while self.status is None:
            child, status = os.waitpid(self.pid, os.WNOHANG)
            if child:
                self.status = status
                break
            if time.monotonic() > deadline:
                raise AssertionError("process did not exit")
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    self.output += os.read(self.master, 65536)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
        assert os.waitstatus_to_exitcode(self.status) == 0, "nonzero process exit"
        assert termios.tcgetattr(self.master) == self.original, "terminal attributes were not restored"

    def close(self):
        if self.status is None:
            child, status = os.waitpid(self.pid, os.WNOHANG)
            if not child:
                os.kill(self.pid, signal.SIGKILL)
                os.waitpid(self.pid, 0)
        os.close(self.master)


def main():
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
    app = Terminal([str(build / "ptoolkit")])
    try:
        app.wait_text(b"macOS")
        app.wait_text(b"cores")
        app.send(b"hello\r")
        app.wait_text(b"hello world")
        app.send(b"home\r")
        # Wait for a freshly rendered hardware page, not the initial frame.
        app.output = b""
        app.wait_text(b"macOS")
        app.send(b"\x03")
        app.finish()
        print("Application PTY: hardware page, command input, page switching, Ctrl+C and termios restore passed")
    finally:
        app.close()

    # Exercise the global-prompt page against an isolated HOME. Nothing under
    # the real ~/.claude is read or written.
    with tempfile.TemporaryDirectory() as home:
        claude_dir = Path(home) / ".claude"
        claude_dir.mkdir()
        (claude_dir / "CLAUDE.md").write_text("# Fixture rules\n",
                                              encoding="utf-8")
        prompt = Terminal([str(build / "ptoolkit")], {"HOME": home})
        try:
            prompt.wait_text(b"macOS")
            prompt.send(b"claude_code_prompt_man\r")
            prompt.wait_text(b"~/.claude/CLAUDE.md")
            prompt.wait_text(b"Fixture rules")
            prompt.send(b"\x03\x1b[17~\x03")
            prompt.finish()
            print("Application PTY: global prompt page, editor focus and termios restore passed")
        finally:
            prompt.close()

    demo = Terminal([str(build / "src/editor/ptoolkit_editor_demo"), "--terminal-probe"])
    try:
        demo.wait_text(b"Standalone FTXUI editor")
        # Same sequence and final document assertions as the native Windows probe.
        demo.send(b"x\x1a\x1b[122;6u\x1b[1;5H\x1b[C\x1b[1;5F\x1b[D"
                  b"\x1b[1;2H\x03\x1b[1;5F\x16\x1b[17~")
        demo.wait_text(b"probe passed")
        demo.finish()
        print("Editor PTY: selection, copy/paste, undo/redo, navigation and termios restore passed")
    finally:
        demo.close()


if __name__ == "__main__":
    main()
