#!/usr/bin/env python3
"""Local Git watcher, preview server and optional Flipper USB CLI bridge."""
import argparse, datetime, json, pathlib, re, subprocess, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

class Watcher:
    def __init__(self, repo, state_path, emit=lambda event: None, feed_on="push"):
        self.repo = str(pathlib.Path(repo).resolve())
        self.path = pathlib.Path(state_path)
        self.emit = emit
        self.feed_on = feed_on
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
        head = self.git('rev-parse', '--verify', 'HEAD')
        try:
            branch = self.git('symbolic-ref', '--short', '-q', 'HEAD')
        except subprocess.CalledProcessError:
            branch = ''
        notifications = []
        event = None
        with self.lock:
            entry = self.data['repos'].setdefault(self.repo, {})
            previous = entry.get('head')
            day = datetime.date.today().isoformat()
            if entry.get('day') != day:
                entry.update(day=day, meals=0)
            if previous and previous != head:
                ancestor = self.is_ancestor(previous, head)
                changed = bool(self.git('diff', '--name-only', previous, head)) if ancestor else False
                if ancestor and changed:
                    entry.update(pending=head, pending_branch=branch)
                    notice = {'kind':'commit', 'time':time.time(), 'label':self.git('log','-1','--format=%s')[:90], 'id':head + ':commit'}
                    self.data['events'] = (self.data['events'] + [notice])[-30:]
                    notifications.append('commit')
                else:
                    entry.pop('pending', None)
            pending = entry.get('pending')
            ready = self.feed_on == 'commit'
            if pending and self.feed_on == 'push' and branch and entry.get('pending_branch') == branch:
                try:
                    upstream = self.git('rev-parse', '--verify', '@{upstream}')
                    ready = self.is_ancestor(pending, upstream)
                except subprocess.CalledProcessError:
                    ready = False
            if pending and ready:
                if entry['meals'] < 3:
                    entry['meals'] += 1
                    event = {'kind':'feed', 'time':time.time(), 'label':self.git('log','-1','--format=%s',pending)[:90], 'id':pending + ':feed'}
                    self.data['events'] = (self.data['events'] + [event])[-30:]
                    notifications.append('feed')
                entry.pop('pending', None)
            entry['head'] = head
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temp = self.path.with_suffix('.tmp')
            temp.write_text(json.dumps(self.data))
            temp.replace(self.path)
        for notification in notifications:
            self.emit(notification)
        return event

    def is_ancestor(self, earlier, later):
        return subprocess.run(['git', '-C', self.repo, 'merge-base', '--is-ancestor', earlier, later], capture_output=True).returncode == 0

    def snapshot(self):
        with self.lock:
            return {'repo':pathlib.Path(self.repo).name, 'events':list(self.data['events']), 'feed_on':self.feed_on, 'pending_push':bool(self.data['repos'].get(self.repo,{}).get('pending')), 'meals':self.data['repos'].get(self.repo,{}).get('meals',0)}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', default='.')
    parser.add_argument('--feed-on', choices=['push', 'commit'], default='push', help='Reward after upstream confirmation (default) or immediately after commit')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--serial', help='Flipper serial port; close qFlipper first')
    parser.add_argument('--state', default=str(pathlib.Path(__file__).with_name('state.json')))
    args = parser.parse_args()
    device = None
    if args.serial:
        import serial
        device = serial.Serial(args.serial, 230400, timeout=0.1, write_timeout=1)
    device_status = {'connected': False}
    device_lock = threading.Lock()
    def exchange(command, marker=b'PIP meals='):
        with device_lock:
            device.reset_input_buffer()
            device.write(('gitgotchi '+command+'\r').encode())
            response=b''
            deadline=time.monotonic()+2
            while time.monotonic()<deadline:
                response+=device.read(device.in_waiting or 1)
                position=response.find(marker)
                if position>=0 and b'>: ' in response[position:]:
                    break
            return response
    def emit(event):
        if device:
            try:
                command = 'feedlocal' if event == 'feed' and args.feed_on == 'commit' else event
                response = exchange(command)
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
    if device:
        try:
            first=exchange('status',b'PIP_CLOCK')
            first_at=time.monotonic()
            time.sleep(.8)
            second=exchange('status',b'PIP_CLOCK')
            second_at=time.monotonic()
            a=re.search(rb'tick=(\d+)',first)
            b=re.search(rb'tick=(\d+)',second)
            if a and b:
                rate=round((int(b[1])-int(a[1]))/(second_at-first_at)/10)*10
                if 300<=rate<=2000:
                    exchange('rate '+str(rate))
                    print('Device animation clock calibrated:',rate,'ticks/s',flush=True)
        except Exception as error:
            print('Clock calibration skipped:',error,flush=True)
    emit('status')
    watcher = Watcher(args.repo, args.state, emit, feed_on=args.feed_on)
    watcher.git('rev-parse', '--show-toplevel')
    def watch():
        while True:
            try:
                watcher.poll()
            except (subprocess.CalledProcessError, OSError) as error:
                print('Waiting for a readable commit:', error, flush=True)
            time.sleep(2)
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/api/state':
                payload = json.dumps(dict(watcher.snapshot(), device=dict(device_status))).encode()
                content_type = 'application/json'
            elif self.path.split('?')[0] == '/api/screen.png':
                if not device:
                    self.send_error(503, 'No Flipper connected')
                    return
                try:
                    from screen import encode_png
                    response = exchange('screenshot', b'PIP_SCREEN ')
                    match = re.search(rb'PIP_SCREEN ([0-9a-f]{2048})', response)
                    if not match:
                        raise ValueError('Open Gitgotchi on Flipper')
                    payload=encode_png(bytes.fromhex(match[1].decode()))
                    content_type='image/png'
                except Exception:
                    self.send_error(503, 'Flipper screen unavailable')
                    return
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
