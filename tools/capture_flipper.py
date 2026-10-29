#!/usr/bin/env python3
"""Capture Gitgotchi's real 128x64 framebuffer through its USB CLI command."""
import argparse
import pathlib
import re
import time
import serial

import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'companion'))
from screen import encode_png

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--action', choices=['pet', 'feed', 'sleep', 'wake', 'commit'])
    parser.add_argument('--wait', type=float, default=1)
    args = parser.parse_args()
    with serial.Serial(args.serial, 230400, timeout=.2, write_timeout=2) as device:
        time.sleep(.3)
        device.reset_input_buffer()
        if args.action:
            device.write(('gitgotchi ' + args.action + '\r').encode())
        time.sleep(args.wait)
        device.reset_input_buffer()
        device.write(b'gitgotchi screenshot\r')
        data = b''
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            data += device.read(device.in_waiting or 1)
            match = re.search(rb'PIP_SCREEN ([0-9a-f]{2048})', data)
            if match:
                pathlib.Path(args.output).write_bytes(encode_png(bytes.fromhex(match[1].decode())))
                print('Saved real Flipper screen:', args.output)
                return
        raise SystemExit('No screen received. Open Gitgotchi and close other serial clients first.')

if __name__ == '__main__':
    main()
