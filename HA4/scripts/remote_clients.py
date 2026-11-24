#!/usr/bin/env python3
"""Launch a complete set of participants against an already running TCP server."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host')
    parser.add_argument('port')
    parser.add_argument('--cabins', type=int, required=True)
    args = parser.parse_args()
    if not 1 <= args.cabins <= 12:
        parser.error('cabins must be between 1 and 12')
    logs = ROOT / 'logs' / 'remote'
    logs.mkdir(parents=True, exist_ok=True)
    roles = [('operator', 'cc_operator', []), ('A', 'cc_station', ['A']),
             ('B', 'cc_station', ['B']), ('observer1', 'cc_observer', []),
             ('observer2', 'cc_observer', ['--no-board'])]
    roles += [(f'cabin{i}', 'cc_cabin', [str(i)]) for i in range(1, args.cabins + 1)]
    processes, files = [], []
    try:
        for label, executable, extra in roles:
            handle = open(logs / (label + '.txt'), 'w')
            files.append(handle)
            process = subprocess.Popen([str(ROOT / executable), args.host, args.port, *extra,
                '--wait', '10'], stdout=handle, stderr=subprocess.STDOUT)
            processes.append((label, process))
        for label, process in processes:
            process.wait(timeout=90)
            if process.returncode != 0:
                raise RuntimeError(f'{label} failed: {(logs / (label + ".txt")).read_text()}')
        text = (logs / 'observer1.txt').read_text()
        if 'Итоги работы' not in text or 'нарушений не обнаружено' not in text:
            raise RuntimeError('observer did not receive valid final statistics')
        print(text)
        print('Distributed clients passed: all clients received shutdown and exited')
    finally:
        for _, process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
        for handle in files:
            handle.close()


if __name__ == '__main__':
    main()
