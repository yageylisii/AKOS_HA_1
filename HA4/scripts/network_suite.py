#!/usr/bin/env python3
"""Local TCP launcher and integration checks. Only Python's standard library is used."""
import argparse
import json
from pathlib import Path
import re
import signal
import socket
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
MAGIC, VERSION = 0x41433436, 2


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Run:
    def __init__(self, name, config='settings_small.txt', delay=0, port=0, seed=12345, role_only=False):
        self.path = ROOT / 'logs' / name
        self.path.mkdir(parents=True, exist_ok=True)
        self.processes = []
        self.files = []
        self.sockets = []
        self.config = ROOT / 'scripts' / config
        values = [line.split('#')[0].strip() for line in self.config.read_text().splitlines()]
        self.cabins = int(next(v for v in values if v))
        if not port:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1]
        self.port = port
        self.delay, self.seed = delay, seed
        self.role_only = role_only

    def __enter__(self):
        self.server = self.start('server', 'cc_server', [str(self.port), '--host', '127.0.0.1',
            '--config', str(self.config), '--delay', str(self.delay), '--seed', str(self.seed),
            '--log', str(self.path / 'events.txt'), *(['--role-only'] if self.role_only else [])])
        self.until('server', 'сервер канатной дороги слушает')
        return self

    def __exit__(self, *_):
        # Stop the server first so connected clients receive SHUTDOWN when possible.
        if self.server.poll() is None:
            self.server.terminate()
        for _, process in self.processes:
            if process.poll() is None:
                try:
                    process.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for sock in self.sockets:
            sock.close()
        for handle in self.files:
            handle.close()

    def text(self, label='server'):
        return (self.path / (label + '.txt')).read_text(errors='replace')

    def start(self, label, executable, args):
        handle = open(self.path / (label + '.txt'), 'w')
        self.files.append(handle)
        process = subprocess.Popen([str(ROOT / executable), *args], cwd=ROOT,
            stdin=subprocess.DEVNULL, stdout=handle, stderr=subprocess.STDOUT)
        self.processes.append((label, process))
        return process

    def client(self, label, executable, *args):
        return self.start(label, executable, ['127.0.0.1', str(self.port), *args, '--wait', '2'])

    def until(self, label, fragment, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if fragment in self.text(label):
                return
            if self.server.poll() is not None:
                raise RuntimeError(f'{label}: server exited before {fragment!r}; {self.text()[-1000:]}')
            time.sleep(.02)
        raise RuntimeError(f'{label}: timeout waiting for {fragment!r}; {self.path}')

    def participants(self, skip=None):
        result = {}
        roles = [('operator', 'cc_operator', []), ('station-A', 'cc_station', ['A']),
                 ('station-B', 'cc_station', ['B'])]
        roles += [(f'cabin-{i}', 'cc_cabin', [str(i)]) for i in range(1, self.cabins + 1)]
        for label, executable, args in roles:
            if label != skip:
                result[label] = self.client(label, executable, *args)
        return result

    def finish(self, failed=(), evacuated=False, timeout=50):
        self.server.wait(timeout=timeout)
        require(self.server.returncode == 0, f'server failed; {self.path}')
        for label, process in self.processes:
            process.wait(timeout=5)
            require(label in failed or process.returncode == 0,
                    f'{label}: exit {process.returncode}; {self.path}')
        text = self.text()
        require('нарушений не обнаружено' in text, f'invariant failure; {self.path}')
        numbers = {}
        for key, title in [('accepted', 'Принято пассажиров'), ('delivered', 'Доставлено пассажиров'),
                           ('evacuated', 'Эвакуировано пассажиров')]:
            match = re.search(title + r':\s+(\d+)', text)
            require(match is not None, f'missing {title}; {self.path}')
            numbers[key] = int(match[1])
        require(numbers['accepted'] == numbers['delivered'] + numbers['evacuated'],
                f'passengers lost; {self.path}')
        if not evacuated:
            require(numbers['evacuated'] == 0, f'unexpected evacuation; {self.path}')
        return numbers

    def raw(self):
        sock = socket.create_connection(('127.0.0.1', self.port), timeout=3)
        self.sockets.append(sock)
        return sock


def basic(config, name):
    with Run(name, config) as run:
        run.client('observer-1', 'cc_observer')
        run.client('observer-2', 'cc_observer', '--no-board')
        run.participants()
        result = run.finish()
        for observer in ['observer-1', 'observer-2']:
            require('Итоги работы' in run.text(observer), f'{observer} missed shutdown')
        return result


def reconnect(demo=False):
    with Run('demo-reconnect' if demo else 'test-reconnect', delay=150) as run:
        observer = run.client('observer-old', 'cc_observer')
        run.client('observer-stable', 'cc_observer')
        roles = run.participants()
        run.until('server', 'начал посадку')
        observer.send_signal(signal.SIGINT)
        roles['cabin-1'].send_signal(signal.SIGINT)
        observer.wait(timeout=4)
        roles['cabin-1'].wait(timeout=4)
        run.until('server', 'моделирование приостановлено')
        # Keep the server alive with a missing participant, then restore its slot.
        time.sleep(.2)
        require(run.server.poll() is None, 'server died after cabin disconnected')
        run.client('cabin-restarted', 'cc_cabin', '1')
        run.client('observer-restarted', 'cc_observer')
        run.until('server', 'моделирование продолжается')
        result = run.finish()
        require('Итоги работы' in run.text('observer-restarted'), 'new observer missed final state')
        if demo:
            print(run.text())
        return result


def stop_server(console=False):
    with Run('test-console' if console else 'test-signal', delay=150) as run:
        # For console commands, replace /dev/null with an actual pipe before starting roles.
        if console:
            run.server.terminate()
            run.server.wait(timeout=4)
            handle = open(run.path / 'server.txt', 'w')
            run.files.append(handle)
            run.server = subprocess.Popen([str(ROOT / 'cc_server'), str(run.port), '--host', '127.0.0.1',
                '--config', str(run.config), '--delay', '150', '--seed', '12345',
                '--log', str(run.path / 'events.txt')], cwd=ROOT, stdin=subprocess.PIPE,
                stdout=handle, stderr=subprocess.STDOUT)
            run.processes = [('server', run.server)]
            run.until('server', 'сервер канатной дороги слушает')
        run.client('observer', 'cc_observer')
        run.participants()
        run.until('server', 'начал посадку')
        if console:
            run.server.stdin.write(b'q\n')
            run.server.stdin.flush()
        else:
            run.server.send_signal(signal.SIGINT)
        result = run.finish(evacuated=True)
        require(result['evacuated'] > 0, 'evacuation was not exercised')
        require('клиент закрывается' in run.text('observer') or
                'клиент закрывается' in run.text('cabin-1'), 'no SHUTDOWN message')
        return result


def killed_participant():
    with Run('test-killed', delay=150) as run:
        roles = run.participants()
        run.until('server', 'начал посадку')
        roles['operator'].kill()
        roles['operator'].wait(timeout=4)
        run.until('server', 'моделирование приостановлено')
        run.client('operator-restarted', 'cc_operator')
        return run.finish(failed=('operator',))


def duplicate_and_partial():
    with Run('test-protocol') as run:
        partial = run.raw()
        partial.sendall(struct.pack('!I', MAGIC)) # Incomplete HELLO must not block accept().
        run.client('operator', 'cc_operator')
        run.until('operator', 'подключено')
        duplicate = run.client('duplicate', 'cc_operator')
        duplicate.wait(timeout=4)
        require(duplicate.returncode != 0 and 'занят' in run.text('duplicate'),
                'duplicate participant was not rejected')
        alien = run.raw()
        alien.sendall(struct.pack('!III', 0xDEADBEEF, 1, 0))
        require(alien.recv(1) == b'', 'bad frame was not disconnected')
        run.client('observer', 'cc_observer')
        run.participants(skip='operator')
        result = run.finish(failed=('duplicate',))
        partial.close()
        return result


def stalled_participant():
    with Run('test-timeout', delay=150) as run:
        roles = run.participants()
        run.until('server', 'начал посадку')
        roles['cabin-1'].send_signal(signal.SIGSTOP)
        try:
            run.until('server', 'не ответил на такт вовремя', timeout=14)
            run.client('cabin-restarted', 'cc_cabin', '1')
            roles['cabin-1'].kill()
            roles['cabin-1'].wait(timeout=4)
            return run.finish(failed=('cabin-1',))
        finally:
            if roles['cabin-1'].poll() is None:
                roles['cabin-1'].kill()


def slow_observer():
    with Run('test-backpressure', 'settings_backpressure.txt') as run:
        slow = socket.socket()
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        slow.settimeout(3)
        slow.connect(('127.0.0.1', run.port))
        run.sockets.append(slow)
        # Version 2 fixed schema is 14772 bytes (also asserted in protocol_test).
        hello = struct.pack('!IIII', VERSION, 14772, 3, 0xFFFFFFFF)
        slow.sendall(struct.pack('!III', MAGIC, 1, len(hello)) + hello)
        run.until('server', 'подключился наблюдатель 1')
        # This socket never reads: a healthy observer must still finish promptly.
        run.client('observer-healthy', 'cc_observer', '--no-board')
        run.participants()
        started = time.monotonic()
        result = run.finish(timeout=20)
        require(time.monotonic() - started < 20, 'slow observer blocked server')
        require('Итоги работы' in run.text('observer-healthy'), 'healthy observer missed shutdown')
        require('переполнена очередь отправки' in run.text() or
                'клиент не читает данные' in run.text(), 'backpressure was not exercised')
        return result


def separate_views():
    with Run('test-separate-views', role_only=True) as run:
        run.participants()
        run.server.wait(timeout=30)
        for label, process in run.processes:
            process.wait(timeout=4)
            require(process.returncode == 0, f'{label} failed in separate views mode')
        require('Пассажир' not in run.text() and 'Итоги работы' not in run.text(),
                'server printed integrated information in base mode')
        require('Пассажир' in run.text('station-A') and 'Кабина' in run.text('cabin-1'),
                'role-specific events are missing')
        return {'role_views_only': True}


def cli_errors():
    commands = [('cc_server', ['0']), ('cc_server', ['65536']),
                ('cc_station', ['127.0.0.1', '1234', 'C']),
                ('cc_cabin', ['127.0.0.1', '1234', '999'])]
    for executable, args in commands:
        result = subprocess.run([str(ROOT / executable), *args], capture_output=True, timeout=4)
        require(result.returncode != 0, f'{executable} accepted bad arguments: {args}')
    return {'rejected_commands': len(commands)}


def tests():
    cases = [('small', lambda: basic('settings_small.txt', 'test-small')),
             ('weather-and-breakdowns', lambda: basic('settings.txt', 'test-weather')),
             ('stress', lambda: basic('settings_stress.txt', 'test-stress')),
             ('reconnect', reconnect), ('signal-shutdown', stop_server),
             ('console-shutdown', lambda: stop_server(console=True)),
             ('killed-participant', killed_participant), ('duplicate-and-partial', duplicate_and_partial),
             ('reply-timeout', stalled_participant), ('slow-observer', slow_observer),
             ('separate-views', separate_views), ('invalid-arguments', cli_errors)]
    results = {}
    for name, action in cases:
        results[name] = action()
        print(f'PASS {name}: {results[name]}', flush=True)
    (ROOT / 'logs' / 'test-results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'Network tests passed: {len(results)} scenarios')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['local', 'reconnect', 'test'])
    parser.add_argument('--config', default='settings.txt')
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--delay', type=int, default=100)
    args = parser.parse_args()
    if args.mode == 'test':
        tests()
    elif args.mode == 'reconnect':
        reconnect(demo=True)
    else:
        with Run('local', args.config, args.delay, args.port) as run:
            run.client('observer', 'cc_observer')
            run.participants()
            print(f'Локальный TCP-сервер: 127.0.0.1:{run.port}; журналы: {run.path}', flush=True)
            position = 0
            while run.server.poll() is None:
                text = run.text('observer')
                print(text[position:], end='', flush=True)
                position = len(text)
                time.sleep(.05)
            run.finish(timeout=600)
            print(run.text('observer')[position:], end='')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired, KeyboardInterrupt) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
