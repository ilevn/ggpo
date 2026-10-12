#!/usr/bin/env python3
"""Two real UDP peers, a deterministic fault proxy, and an independent state oracle."""
import heapq
import os
from pathlib import Path
import re
import selectors
import socket
import struct
import subprocess
import sys
import time

binary, scenario, output = sys.argv[1:]
binary = str(Path(binary).resolve())
output = Path(output).resolve()
output.mkdir(parents=True, exist_ok=True)
processes, files, sockets = [], [], []

def sock():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 0))
    s.setblocking(False)
    sockets.append(s)
    return s

def launch(mode, player, local, remote, spectator=0):
    directory = output / f'peer{len(processes)}'
    directory.mkdir(exist_ok=True)
    log = open(directory / 'output.log', 'w')
    files.append(log)
    env = os.environ.copy()
    # Logging exercises configuration, formatting and va_list portability too.
    env['ggpo.log'] = 'true'
    env['ggpo.log.timestamps'] = 'true'
    p = subprocess.Popen([binary, mode, str(player), str(local), str(remote), str(spectator)],
                         cwd=directory, stdout=log, stderr=subprocess.STDOUT, env=env)
    processes.append(p)

try:
    if scenario == 'synctest':
        launch('synctest', 0, 0, 0)
        processes[0].wait(timeout=40)
    else:
        # Reserve peer ports until immediately before launching each process.
        reserved = [sock(), sock(), sock()]
        ports = [s.getsockname()[1] for s in reserved]
        proxies = [sock(), sock()]
        proxy_ports = [s.getsockname()[1] for s in proxies]
        selector = selectors.DefaultSelector()
        for i, s in enumerate(proxies): selector.register(s, selectors.EVENT_READ, i)
        spectator_port = ports[2] if scenario == 'spectator' else 0
        if spectator_port:
            reserved[2].close()
            launch('spectator', 0, ports[2], ports[0])
        for i in range(2):
            reserved[i].close()
            launch('disconnect' if scenario.startswith('disconnect') else 'peer', i, ports[i], proxy_ports[i], spectator_port)
        queue, serial, packets = [], 0, 0
        first_input = None
        dropped = duplicated = reordered = 0
        deadline = time.monotonic() + 45
        while any(p.poll() is None for p in processes):
            if any(p.poll() not in (None, 0) for p in processes):
                break
            now = time.monotonic()
            if now > deadline: raise RuntimeError('test deadline exceeded')
            for key, _ in selector.select(0.001):
                direction = key.data
                while True:
                    try: data, _ = key.fileobj.recvfrom(8192)
                    except BlockingIOError: break
                    now = time.monotonic()
                    is_input = len(data) >= 5 and data[4] == 3
                    if is_input:
                        packets += 1
                        # Start outages after real play, not while one peer may
                        # still be finishing the handshake. Test startup separately.
                        if first_input is None and len(data) >= 33 and struct.unpack_from('<I', data, 21)[0] >= 30:
                            first_input = now
                    if scenario == 'disconnect_startup' and len(data) >= 5 and data[4] not in (1, 2):
                        dropped += 1
                        continue
                    age = 0 if first_input is None else now - first_input
                    if first_input is not None and ((scenario == 'recovery' and 0.4 < age < 0.8) or
                                                    (scenario == 'disconnect' and age > 0.4)):
                        dropped += 1
                        continue
                    delay = 0.008 if is_input and scenario in ('delayed', 'impaired', 'recovery') else 0
                    if scenario == 'impaired' and is_input:
                        if packets % 11 == 0:
                            dropped += 1
                            continue
                        if packets % 7 == 0:
                            delay += 0.025
                            reordered += 1
                    serial += 1
                    heapq.heappush(queue, (now + delay, serial, 1-direction, data))
                    if scenario == 'impaired' and is_input and packets % 17 == 0:
                        serial += 1; duplicated += 1
                        heapq.heappush(queue, (now + delay + 0.003, serial, 1-direction, data))
            while queue and queue[0][0] <= time.monotonic():
                _, _, destination, data = heapq.heappop(queue)
                proxies[destination].sendto(data, ('127.0.0.1', ports[destination]))
        selector.close()
    for f in files: f.flush()
    for i, p in enumerate(processes):
        if p.poll() not in (None, 0):
            text = (output / f'peer{i}' / 'output.log').read_text()
            raise RuntimeError(f'peer {i} exited {p.returncode}:\n{text[-7000:]}')
    results = []
    for i, p in enumerate(processes):
        text = (output / f'peer{i}' / 'output.log').read_text()
        if p.returncode != 0:
            raise RuntimeError(f'peer {i} exited {p.returncode}:\n{text[-7000:]}')
        match = re.search(r'RESULT (.*)', text)
        if not match: raise RuntimeError(f'peer {i} did not produce a result: {text[-2000:]}')
        result = dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', match[1]))
        results.append(result)
    if scenario == 'impaired': assert dropped and duplicated and reordered
    if scenario in ('recovery', 'disconnect', 'disconnect_startup'): assert dropped
    if not scenario.startswith('disconnect'):
        assert len({r['hash'] for r in results}) == 1, results
        assert all(r['frame'] >= 600 for r in results), results
    if scenario in ('delayed', 'impaired', 'recovery'):
        assert all(r['loads'] > 0 and r['replays'] > 0 for r in results), results
    if scenario == 'recovery':
        assert any(r['stalls'] > 0 for r in results), results
        assert all(r['interrupted'] and r['resumed'] for r in results), results
    print(scenario, results)
except Exception:
    print(f'Logs retained in {output}', file=sys.stderr)
    raise
finally:
    for p in processes:
        if p.poll() is None: p.kill()
        p.wait()
    for f in files: f.close()
    for s in sockets: s.close()
