#!/usr/bin/env python3
"""Real POSIX PTYs, production CLI/transport, no hardware or RF commands.

Pass a host-built tdvp-nrf52840 executable. Responses below are protocol fixtures,
not evidence of a physical module, successful RF or a BlueZ adapter.
"""
import fcntl
import json
import os
import pty
import select
import signal
import subprocess
import sys
import termios
import time

BINARY = os.path.abspath(sys.argv[1])
VERSION = b'+VER:K230_NRF52840_AT,2026-08-14\r\nOK\r\n'
READY = b'+STATUS:READY,COUNT=0,GATT=0,SVC=0,CHR=0,DESC=0,MESH_ADV=1,MESH_CONN=0,MESH_QUEUE=0\r\nOK\r\n'
START = [(b'AT+VER?', VERSION), (b'AT+STATUS?', READY)]
SCAN = b'OK\r\n+SCAN:0,AA:BB:CC:DD:EE:01,-40,0,first\r\n+SCAN:1,12:34:56:78:9A:BC,-51,1,second,with comma\r\n+SCAN:DONE,2\r\n'
CONNECT = START + [(b'AT+SCAN=1', SCAN), (b'AT+CONN=1', b'OK\r\n+CONNECTED:0\r\n')]
COMMANDS = ['scan 1', 'connect 12:34:56:78:9A:BC']
passed = 0


def case(name, commands, exchanges, *, success=True, fragment=False, locked=False,
         cancel=False, forbidden_completion=None):
    global passed
    master, slave = pty.openpty()
    before = termios.tcgetattr(slave)
    if locked:
        fcntl.flock(slave, fcntl.LOCK_EX | fcntl.LOCK_NB)
    proc = subprocess.Popen([BINARY, '--device', os.ttyname(slave), '--timeout-ms', '150', *commands],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    received = bytearray()
    emitted = []
    signalled = False
    try:
        deadline = time.monotonic() + 5
        while proc.poll() is None and time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.01)
            if not ready:
                continue
            received.extend(os.read(master, 8192))
            while b'\r\n' in received:
                wire, _, tail = received.partition(b'\r\n')
                received = bytearray(tail)
                index = len(emitted)
                assert index < len(exchanges), (name, 'unexpected command', wire)
                expected, response = exchanges[index]
                assert wire == expected, (name, expected, wire)
                emitted.append(bytes(wire))
                if cancel:
                    proc.send_signal(signal.SIGTERM)
                    signalled = True
                elif fragment:
                    for i in range(0, len(response), 3):
                        os.write(master, response[i:i+3])
                        time.sleep(0.001)
                else:
                    os.write(master, response)
        out, err = proc.communicate(timeout=1)
        assert not received, (name, 'unterminated command', received)
        assert len(emitted) == len(exchanges), (name, emitted, exchanges, out, err)
        assert (proc.returncode == 0) == success, (name, proc.returncode, out, err)
        assert termios.tcgetattr(slave) == before, (name, 'termios not restored')
        records = [json.loads(line) for line in out.splitlines()]
        errors = [json.loads(line) for line in err.splitlines()]
        if success:
            assert not errors, (name, errors)
            assert sum('completed' in record for record in records) == 1 + len(commands)
        else:
            assert errors and all('error' in record for record in errors), (name, out, err)
        if forbidden_completion:
            assert not any(record.get('completed') == forbidden_completion for record in records)
        if cancel:
            assert signalled and any('cancel' in e['error'] for e in errors)
        if locked:
            fcntl.flock(slave, fcntl.LOCK_UN)
        fcntl.flock(slave, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fcntl.flock(slave, fcntl.LOCK_UN)
        passed += 1
        print('PASS', name)
        return records
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        os.close(master)
        os.close(slave)


case('fragmented identity and status', [], START, fragment=True)
case('silent UART bounded', [], [(b'AT+VER?', b'')], success=False)
case('identity without OK is not success', [], [(b'AT+VER?', VERSION[:-4])], success=False)
case('OK without identity is not success', [], [(b'AT+VER?', b'OK\r\n')], success=False)
case('wrong firmware does not trigger status or RF', ['scan 1'],
     [(b'AT+VER?', b'+VER:OTHER,1\r\nOK\r\n')], success=False)
case('malformed status rejected', [], START[:1] + [(b'AT+STATUS?', b'+STATUS:READY\r\nOK\r\n')], success=False)
case('no reset command route', ['AT+RESET'], START, success=False)
case('no command injection', ['status\nAT+DFU'], START, success=False)
case('exclusive owner prevents any bytes', [], [], success=False, locked=True)
case('SIGTERM restores UART', [], [(b'AT+VER?', b'')], success=False, cancel=True)
case('oversized line rejected', [], [(b'AT+VER?', b'X' * 2300)], success=False)
case('embedded control character rejected', [], [(b'AT+VER?', b'+VER:K230_NRF52840_AT,\x1b[31m\r\nOK\r\n')], success=False)
case('scan ACK without DONE is not completion', ['scan 1', 'status'],
     START + [(b'AT+SCAN=1', b'OK\r\n')], success=False, forbidden_completion='scan 1')
case('scan count mismatch', ['scan 1'], START + [(b'AT+SCAN=1', b'OK\r\n+SCAN:DONE,1\r\n')], success=False)
case('no connection to unscanned address', ['connect 12:34:56:78:9A:BC'], START, success=False)
case('numeric-leading address resolved to correct index', COMMANDS, CONNECT)
case('connect ACK without event is not completion', COMMANDS,
     CONNECT[:-1] + [(b'AT+CONN=1', b'OK\r\n')], success=False, forbidden_completion=COMMANDS[-1])
busy = READY.replace(b'READY', b'SCANNING')
case('does not take over existing scan', ['scan 1'], START[:1] + [(b'AT+STATUS?', busy)], success=False)
connected = READY.replace(b'READY', b'CONNECTED,0')
case('does not take over existing connection', ['read 37'], START[:1] + [(b'AT+STATUS?', connected)], success=False)
case('wrong read handle fails and no later request', COMMANDS + ['read 37', 'status'],
     CONNECT + [(b'AT+READ=37,0', b'OK\r\n+READ:38,0,1,AA,0\r\n')], success=False, forbidden_completion='read 37')
case('wrong payload length rejected', COMMANDS + ['read 37'],
     CONNECT + [(b'AT+READ=37,0', b'OK\r\n+READ:37,0,2,AA,0\r\n')], success=False)
case('GATT error is not write success', COMMANDS + ['write 37 AA'],
     CONNECT + [(b'AT+WRITE=37,AA,REQ', b'OK\r\n+WRITE:37,1,105\r\n')], success=False)
case('reset invalidates request', COMMANDS + ['read 37'],
     CONNECT + [(b'AT+READ=37,0', b'OK\r\n+BOOT:K230_NRF52840_AT\r\n+READ:37,0,1,AA,0\r\n')], success=False)
case('disconnect invalidates GATT request', COMMANDS + ['read 37'],
     CONNECT + [(b'AT+READ=37,0', b'OK\r\n+DISCONNECTED:0,13\r\n')], success=False)
case('orphan OK cannot satisfy next command', ['status'],
     START[:1] + [(b'AT+STATUS?', READY + b'OK\r\n')], success=False)
case('duplicate result cannot masquerade as one response', COMMANDS + ['read 37'],
     CONNECT + [(b'AT+READ=37,0', b'+READ:37,0,1,AA,0\r\n+READ:37,0,1,BB,0\r\nOK\r\n')], success=False)
case('malformed discovery UUID', COMMANDS + ['services'],
     CONNECT + [(b'AT+GATTS?', b'OK\r\n+GATTS:0,garbage,1,50\r\n+GATTS:DONE,1,10A\r\n')], success=False)
case('characteristic from wrong service', COMMANDS + ['characteristics 0'],
     CONNECT + [(b'AT+GATTC=0', b'OK\r\n+GATTC:0,1,0x2A19:1,36,37,40,18,38\r\n+GATTC:DONE,0,1,10A\r\n')], success=False)
case('write rejects odd hex without sending', COMMANDS + ['write 37 A'], CONNECT, success=False)
case('write rejects oversized data without sending', COMMANDS + ['write 37 ' + 'AA' * 245], CONNECT, success=False)
case('GATT watchdog event invalidates session', COMMANDS + ['read 37', 'status'],
     CONNECT + [(b'AT+READ=37,0', b'OK\r\n+GATT:TIMEOUT\r\n')], success=False)
case('firmware rejection is not acceptance', ['scan 1'],
     START + [(b'AT+SCAN=1', b'ERR:BUSY\r\n')], success=False)

records = case('full asynchronous BLE interaction with notifications',
    COMMANDS + ['services', 'characteristics 0', 'descriptors 0', 'read 37',
                'write 37 AABB', 'cccd 38 notify', 'listen 1', 'disconnect'],
    CONNECT + [
        (b'AT+GATTS?', b'OK\r\n+GATTS:0,0x180F:1,1,50\r\n+GATTS:DONE,1,10A\r\n'),
        (b'AT+GATTC=0', b'OK\r\n+GATTC:0,0,0x2A19:1,36,37,40,18,38\r\n+GATTC:DONE,0,1,10A\r\n'),
        (b'AT+GATTD=0', b'+GATTD:0,0,0x2902:1,38\r\n+GATTD:DONE,0,1,10A\r\nOK\r\n'),
        (b'AT+READ=37,0', b'OK\r\n+NOTIFY:37,1,11\r\n+READ:37,0,2,AABB,0\r\n'),
        (b'AT+WRITE=37,AABB,REQ', b'+WRITE:37,2,0\r\nOK\r\n'),
        (b'AT+WRITE=38,0100,REQ', b'OK\r\n+WRITE:38,2,0\r\n+NOTIFY:37,1,22\r\n'),
        (b'AT+DISC', b'+DISCONNECTED:0,16\r\nOK\r\n'),
    ])
assert any(r.get('event') == '+NOTIFY:37,1,11' for r in records)
assert any(r.get('event') == '+NOTIFY:37,1,22' for r in records)
print(f'{passed} production-transport PTY scenarios PASS; not RF/hardware acceptance')
