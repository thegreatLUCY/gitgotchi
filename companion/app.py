#!/usr/bin/env python3
"""Local Git watcher, preview server and optional Flipper USB CLI bridge."""
import argparse, datetime, json, pathlib, re, subprocess, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

class Watcher:
    def __init__(self, repo, state_path, emit=lambda event: None):
        self.repo = str(pathlib.Path(repo).resolve())
        self.path = pathlib.Path(state_path)
        self.emit = emit
        self.lock = threading.Lock()
        try:
            self.data = json.loads(self.path.read_text())
        except (FileNotFoundError, ValueError):
            self.data = {}
        self.data.setdefault('repos', {})
        self.data.setdefault('events', [])

    def git(self, *args):
        return subprocess.run(['git', '-C', self.repo, *args], capture_output=True, text=True, check=True).stdout.strip()

    def poll(self):
        # A new repository establishes a baseline; history is never awarded as new work.
        head = self.git('rev-parse', '--verify', 'HEAD')
        with self.lock:
            entry = self.data['repos'].setdefault(self.repo, {})
            previous = entry.get('head')
            day = datetime.date.today().isoformat()
            if entry.get('day') != day:
                entry.update(day=day, meals=0)
            event = None
            if previous and previous != head:
                # Count only descendant movement. Checkout/rebase/reset isn't a meal.
                ancestor = subprocess.run(['git', '-C', self.repo, 'merge-base', '--is-ancestor', previous, head], capture_output=True).returncode == 0
                changed = bool(self.git('diff', '--name-only', previous, head)) if ancestor else False
                if ancestor and changed and entry['meals'] < 3:
                    entry['meals'] += 1
                    event = {'kind':'feed', 'time':time.time(), 'label':self.git('log','-1','--format=%s')[:90], 'id':head}
                    self.data['events'] = (self.data['events'] + [event])[-30:]
            entry['head'] = head
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temp = self.path.with_suffix('.tmp')
            temp.write_text(json.dumps(self.data))
            temp.replace(self.path)
        if event:
            self.emit('feed')
        return event

    def snapshot(self):
        with self.lock:
            return {'repo':pathlib.Path(self.repo).name, 'events':list(self.data['events']), 'meals':self.data['repos'].get(self.repo,{}).get('meals',0)}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', default='.')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--serial', help='Flipper serial port; close qFlipper first')
    parser.add_argument('--state', default=str(pathlib.Path(__file__).with_name('state.json')))
    args = parser.parse_args()
    device = None
    if args.serial:
        import serial
        device = serial.Serial(args.serial, 230400, timeout=0.1, write_timeout=1)
    device_status = {'connected': False}
    def emit(event):
        if device:
            try:
                device.write(('gitgotchi '+event+'\r').encode())
                response = b''
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    response += device.read(device.in_waiting or 1)
                    if b'PIP meals=' in response and b'>: ' in response:
                        break
                match = re.search(rb'PIP meals=(\d+) bond=(\d+) mood=(\d+)', response)
                if match:
                    device_status.update(connected=True, meals=int(match[1]), bond=int(match[2]), mood=int(match[3]))
                    print('USB confirmed:', event, device_status, flush=True)
                else:
                    device_status.update(connected=False)
                    print('USB event unconfirmed. Open Gitgotchi on your Flipper.', flush=True)
            except Exception as error:
                device_status.update(connected=False)
                print('USB unavailable:', error, flush=True)
    emit('status')
    watcher = Watcher(args.repo, args.state, emit)
    watcher.git('rev-parse', '--show-toplevel')
    def watch():
        while True:
            try:
                watcher.poll()
            except (subprocess.CalledProcessError, OSError) as error:
                print('Waiting for a readable commit:', error, flush=True)
            if device:
                try:
                    device.read(device.in_waiting or 1)
                except Exception:
                    pass
            time.sleep(2)
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/api/state':
                payload = json.dumps(dict(watcher.snapshot(), device=dict(device_status))).encode()
                content_type = 'application/json'
            elif self.path in ('/', '/index.html'):
                payload = pathlib.Path(__file__).with_name('index.html').read_bytes()
                content_type = 'text/html; charset=utf-8'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(payload)
        def log_message(self, *args):
            pass
    threading.Thread(target=watch, daemon=True).start()
    print(f'Pip is awake: http://127.0.0.1:{args.port} — watching {watcher.repo}', flush=True)
    try:
        ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
    except KeyboardInterrupt:
        print('Pip companion stopped.', flush=True)
    finally:
        if device:
            device.close()

if __name__ == '__main__':
    main()
