#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors
"""Run the real CLI against a Linux PTY programmer model, including failure paths."""
import binascii
import errno
import os
import pathlib
import pty
import select
import subprocess
import sys
import tempfile
import threading

exe = sys.argv[1]
widths = {**{r: 86 for r in range(108)}, **{r: 86 for r in range(128, 229)}, 256: 32, 512: 4, 768: 16}

def blank():
    return {r: ((1 << n)-1).to_bytes((n+7)//8, 'little').hex().upper() for r, n in widths.items()}

def run(action, jed, fault=None):
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    commands, errors = [], []
    memory = blank()
    stopped = threading.Event()
    activated = False
    staged_reads = set()
    def server():
        nonlocal activated
        buf = b''
        active = False
        programmed = False
        try:
            while not stopped.is_set():
                if not select.select([master], [], [], .05)[0]:
                    continue
                try:
                    data = os.read(master, 4096)
                except OSError as e:
                    if e.errno == errno.EIO:
                        continue
                    raise
                buf += data
                while b'\n' in buf:
                    packet, buf = buf.split(b'\n', 1)
                    body, crc = packet.rsplit(b'*', 1)
                    assert binascii.crc_hqx(body, 0xffff) == int(crc, 16)
                    seq, cmd = body.decode().split(' ', 1)
                    commands.append(cmd)
                    result = 'OK'
                    if cmd == 'HELLO': result += ' ATF1502 1'
                    elif cmd == 'ID': result += ' ' + ('0150403F' if fault == 'id' else '0150203F')
                    elif cmd == 'BEGIN': active = True
                    elif cmd == 'END': active = False
                    elif cmd == 'ERASE':
                        assert active
                        memory.update(blank())
                    elif cmd.startswith('WRITE '):
                        assert active
                        _, addr, hexdata = cmd.split()
                        row = int(addr, 16)
                        assert len(bytes.fromhex(hexdata)) == (widths[row]+7)//8
                        if row == 512: assert hexdata == '0F'
                        if row == 256 and not (int.from_bytes(bytes.fromhex(hexdata), 'little') >> 31):
                            assert staged_reads == set(widths), 'Armed before verifying every row'
                            activated = True
                        # Flash cells can only change from 1 to 0 until erased.
                        memory[row] = bytes(a & b for a, b in zip(bytes.fromhex(memory[row]), bytes.fromhex(hexdata))).hex().upper()
                        programmed = True
                    elif cmd.startswith('READ '):
                        assert active
                        row = int(cmd.split()[1], 16)
                        result += ' ' + memory[row]
                        if programmed: staged_reads.add(row)
                        if fault == 'verify' and programmed and row == 0:
                            result = 'OK ' + '00'*11
                        if fault == 'blank' and not programmed and row == 0:
                            result = 'OK ' + '00'*11
                    else: raise AssertionError(cmd)
                    response = f'{seq} {result}'.encode()
                    if fault == 'sequence' and cmd == 'ID': response = f'999 {result}'.encode()
                    check = binascii.crc_hqx(response, 0xffff)
                    if fault == 'crc' and cmd == 'ID': check ^= 1
                    response += f'*{check:04X}\r\n'.encode()
                    # Split responses to exercise partial reads.
                    os.write(master, response[:3]); os.write(master, response[3:])
        except BaseException as e:
            errors.append(e)
    thread = threading.Thread(target=server)
    thread.start()
    args = [exe, action]
    if action in ('flash', 'verify'): args.append(str(jed))
    args += ['--port', port]
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=12)
    finally:
        stopped.set(); thread.join(); os.close(master); os.close(slave)
    if errors: raise errors[0]
    return result, commands, activated

with tempfile.TemporaryDirectory() as tmp:
    jed = pathlib.Path(tmp)/'test.jed'
    f = [0]*16808
    f[0] = 1
    f[16782:16786] = [1]*4
    checksum = sum(bit << (i%8) for i, bit in enumerate(f)) & 65535
    jed.write_bytes(('\x02test*QF16808*F0*L0 '+''.join(map(str,f))+f'*C{checksum:04X}*\x030000').encode())
    result, commands, activated = run('flash', jed)
    assert result.returncode == 0, result.stderr
    assert activated and commands[-1] == 'END'
    assert sum(c.startswith('WRITE ') for c in commands) == 213
    for fault in ('id','crc','sequence','blank','verify'):
        result, commands, activated = run('flash',jed,fault)
        assert result.returncode != 0, fault
        assert not activated, fault
        if fault in ('id','crc','sequence'):
            assert 'BEGIN' not in commands and 'ERASE' not in commands
        if fault == 'blank': assert not any(c.startswith('WRITE') for c in commands)
        if fault == 'verify': assert commands[-1] == 'END'
    result, commands, activated = run('erase',jed)
    assert result.returncode == 0 and not activated, result.stderr
    assert sum(c.startswith('READ') for c in commands) == 212
    result, commands, activated = run('verify',jed)
    assert result.returncode != 0 and 'ERASE' not in commands
    assert not any(c.startswith('WRITE') for c in commands)
    # A malformed file must be rejected before the port is opened.
    jed.write_text('invalid')
    result = subprocess.run([exe,'flash',str(jed),'--port','nonexistent'],capture_output=True,text=True)
    assert result.returncode and 'STX/ETX' in result.stderr
print('Serial integration tests passed')
