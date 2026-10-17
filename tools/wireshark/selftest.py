#!/usr/bin/env python3
"""Check the dissector against synthetic captures, one per generation.

The dissector is the one part of this tree with no test at all, and the part
most exposed to the protocol moving under it: it restates the header maps and
the opcode table by hand, in a language the compiler never sees. A
transcription slip there is invisible until somebody reads a capture and
believes it.

What this checks is the summary line tshark prints, which is the dissector's
own count of what it saw. That is enough to catch a map that claims a pixel
is a trigger or the other way about -- the failure the second generation
introduced -- without asserting the whole tree and so freezing every field
label.

Needs tshark. Run with the path to the script, or from the repository root:

    python3 tools/wireshark/selftest.py

The capture is built here rather than committed because a hand-built one is
readable: every datum below is one line of Python, against a binary blob
nobody can review.
"""

import os
import re
import struct
import subprocess
import sys
import tempfile

DATA_PORT = 1556


def md(header, payload=0):
    """One measurement-data word: a header nibble over a 44-bit payload."""
    return ((header & 0xF) << 44 | (payload & ((1 << 44) - 1))).to_bytes(6, "little")


def datagram(payload, port):
    """The payload inside UDP inside IPv4 inside Ethernet, as a capture needs."""
    udp = struct.pack("!HHHH", port, port, 8 + len(payload), 0) + payload
    ip = struct.pack(
        "!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 1, 0, 64, 17, 0,
        bytes([192, 0, 2, 1]), bytes([192, 0, 2, 2]),
    )
    return b"\x02" * 6 + b"\x03" * 6 + b"\x08\x00" + ip + udp


def write_pcap(path, frames):
    with open(path, "wb") as f:
        f.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        for frame in frames:
            f.write(struct.pack("<IIII", 0, 0, len(frame), len(frame)))
            f.write(frame)


def dissect(script, pcap, generation, home):
    """The one summary line tshark prints for the single frame in pcap."""
    env = dict(os.environ)
    # A copy of this script installed in the user's own plugin directory would
    # be loaded as well, and two protocols of one description is an error.
    env["HOME"] = home
    out = subprocess.run(
        ["tshark", "-r", pcap, "-X", "lua_script:" + script,
         "-o", "katherine.generation:" + generation],
        capture_output=True, text=True, env=env, check=False,
    )
    if out.returncode != 0:
        raise SystemExit("tshark failed:\n" + out.stderr)
    if "Dissector bug" in out.stdout or "Dissector bug" in out.stderr:
        raise SystemExit("dissector bug reported:\n" + out.stdout + out.stderr)
    return out.stdout.strip()


CASES = [
    # (name, generation preference, words, expected fragments of the summary)
    (
        "second generation, four chips",
        "gen2",
        [md(0x7), md(0x0), md(0x1), md(0x2), md(0x3), md(0x4), md(0x6), md(0xC, 5)],
        ["8 MDs: 4 pixels", "chip 0:1, 1:1, 2:1, 3:1", "new frame",
         "frame finished (n_sent=5)", "trigger event"],
    ),
    (
        "first generation",
        "gen1",
        [md(0x7), md(0x4), md(0x4), md(0x2), md(0x5), md(0xD, 3), md(0xC, 2), md(0xE)],
        ["8 MDs: 2 pixels", "new frame", "frame finished (n_sent=2)", "lost=3",
         "aborted", "trigger info", "timestamp offset"],
    ),
    (
        # The failure the generation preference exists to prevent, asserted
        # rather than described: read with the wrong map, the same capture
        # reports the trigger word as a pixel and two pixels as triggers.
        "second-generation capture read as the first",
        "gen1",
        [md(0x7), md(0x0), md(0x1), md(0x2), md(0x3), md(0x4), md(0x6), md(0xC, 5)],
        ["8 MDs: 1 pixel", "trigger info", "unknown=3"],
    ),
]


def opcode_parity(script):
    """Every control opcode the library defines is named by the dissector.

    The dissector restates cmd_interface.h by hand, in a language no compiler
    reads, so the two drift silently: a capture is then annotated with a name
    nobody has checked, or with nothing at all. This is the only thing that
    compares them.
    """
    header = os.path.join(os.path.dirname(os.path.abspath(script)),
                          "..", "..", "c", "src", "protocol", "cmd_interface.h")
    header = os.path.normpath(header)
    if not os.path.exists(header):
        return None, "cmd_interface.h not beside the dissector"

    with open(header, encoding="utf-8") as f:
        library = {int(m.group(2), 16)
                   for m in re.finditer(r"CMD_TYPE_([A-Z0-9_]+)\s*=\s*0x([0-9a-fA-F]+)", f.read())}
    with open(script, encoding="utf-8") as f:
        dissector = {int(m.group(1), 16)
                     for m in re.finditer(r"\[0x([0-9a-fA-F]{2})\]\s*=\s*\"", f.read())}

    missing = sorted(library - dissector)
    if missing:
        return False, "opcodes the library defines and the dissector does not: " \
                      + ", ".join("0x%02x" % o for o in missing)
    return True, "%d opcodes, all named" % len(library)


def main():
    script = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "katherine.lua")
    if not os.path.exists(script):
        raise SystemExit("no such script: " + script)

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        home = os.path.join(tmp, "home")
        os.mkdir(home)

        for i, (name, generation, words, expected) in enumerate(CASES):
            pcap = os.path.join(tmp, "case%d.pcap" % i)
            write_pcap(pcap, [datagram(b"".join(words), DATA_PORT)])
            summary = dissect(script, pcap, generation, home)

            missing = [e for e in expected if e not in summary]
            if missing:
                failures += 1
                print("not ok %d - %s" % (i + 1, name))
                print("#   summary: %s" % summary)
                for e in missing:
                    print("#   missing: %s" % e)
            else:
                print("ok %d - %s" % (i + 1, name))

    ok, detail = opcode_parity(script)
    n = len(CASES) + 1
    if ok is None:
        print("ok %d - opcode parity # SKIP %s" % (n, detail))
    elif ok:
        print("ok %d - opcode parity (%s)" % (n, detail))
    else:
        failures += 1
        print("not ok %d - opcode parity" % n)
        print("#   %s" % detail)

    print("1..%d" % n)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
