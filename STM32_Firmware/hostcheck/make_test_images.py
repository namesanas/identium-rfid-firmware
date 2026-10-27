#!/usr/bin/env python3
"""
Test input for boot_image_host_test.c: a throwaway signing key, its
public half as C, and one signed image per board, all made with
Tools/fw_sign.py so the test covers the real signing path.

Usage: make_test_images.py <output dir>
"""

import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIGN = os.path.join(HERE, "..", "Tools", "fw_sign.py")


def run(*args):
    subprocess.run([sys.executable, SIGN, *args], check=True, stdout=subprocess.DEVNULL)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    key = os.path.join(out, "test_key.pem")
    if os.path.exists(key):
        os.remove(key)
    run("genkey", "--key", key, "--public-c", os.path.join(out, "test_public_key.c"))

    rng = random.Random(20261027)
    for board, size in (("f429", 150001), ("f411", 90002)):
        body = bytearray(rng.randrange(256) for _ in range(size))
        struct.pack_into("<II", body, 0, 0x20020000, 0x08004000 + 0x1235)
        binary = os.path.join(out, f"test_{board}.bin")
        with open(binary, "wb") as f:
            f.write(body)
        run("sign", "--key", key, "--board", board, "--version", "v9.8.7-test",
            binary, os.path.join(out, f"test_{board}.tlfw"))


if __name__ == "__main__":
    main()
