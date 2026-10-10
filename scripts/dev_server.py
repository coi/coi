#!/usr/bin/env python3
"""
Development server for Coi projects.
Supports SPA routing and optional hot reloading via Server-Sent Events.
"""

import http.server
import os
import subprocess
import sys
import time
import threading
from pathlib import Path

# ANSI colors
DIM = ''  
RESET = '\033[0m'
GREEN = '\033[32m'
YELLOW = '\033[33m'
RED = '\033[31m'
BRAND = '\033[38;5;141m'

# Global state
hot_reload_enabled = False
sse_clients = []
sse_lock = threading.Lock()
reload_event = threading.Event()


DEV_SERVICE_WORKER = b"""// coi dev: removes the service worker of a PWA build and its caches
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (e) => e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k.startsWith('coi-')).map((k) => caches.delete(k))))
    .then(() => self.registration.unregister())));
"""


class DevHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler with SPA routing and optional hot reload support."""
    
    def log_message(self, format, *args):
        pass  # Suppress logging
    
    def do_GET(self):
        global hot_reload_enabled
        
        # Handle SSE endpoint for hot reload
        if self.path == '/__hot_reload' and hot_reload_enabled:
            self.handle_sse()
            return
        
        # A PWA build's service worker would serve cached files and hide your edits.
        # Answer with one that removes itself (and any worker left from a real build).
        if self.path.split('?')[0].endswith('/sw.js'):
            self.serve_bytes(DEV_SERVICE_WORKER, 'text/javascript')
            return

        path = self.translate_path(self.path)
        
        if os.path.isfile(path):
            if path.endswith('.html'):
                self.serve_html_with_reload(path)
            else:
                super().do_GET()
            return
        
        # SPA fallback
        self.path = '/index.html'
        index_path = self.translate_path(self.path)
        if os.path.isfile(index_path):
            self.serve_html_with_reload(index_path)
        else:
            self.send_error(404)

    def serve_bytes(self, content, content_type):
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', len(content))
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()
        self.wfile.write(content)
    
    def serve_html_with_reload(self, path):
        """Mark the page as dev (no service worker) and inject the hot reload script."""
        try:
            with open(path, 'rb') as f:
                content = f.read()
            content = content.replace(b'<head>', b'<head><script>window.__coi_dev=1</script>', 1)
            if not hot_reload_enabled:
                self.serve_bytes(content, 'text/html; charset=utf-8')
                return
            
            script = b'''<script>(function(){var k='__coi_scroll';if('scrollRestoration' in history)history.scrollRestoration='manual';var s=sessionStorage.getItem(k);if(s){sessionStorage.removeItem(k);var y=parseInt(s);var n=0;function r(){if(n++>30)return;window.scrollTo(0,y);if(Math.abs(window.scrollY-y)>1)setTimeout(r,60)}window.addEventListener('load',function(){requestAnimationFrame(r)});document.addEventListener('DOMContentLoaded',function(){requestAnimationFrame(r)})}var e=new EventSource('/__hot_reload');e.onmessage=function(m){if(m.data==='reload'){sessionStorage.setItem(k,window.scrollY||document.documentElement.scrollTop);location.reload()}};e.onerror=function(){console.log('[Coi] Reconnecting...')}})();</script></body>'''
            content = content.replace(b'</body>', script)
            
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', len(content))
            self.send_header('Cache-Control', 'no-cache')
            self.end_headers()
            self.wfile.write(content)
        except Exception as e:
            self.send_error(500, str(e))
    
    def handle_sse(self):
        """Server-Sent Events for hot reload."""
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'keep-alive')
        self.end_headers()
        
        with sse_lock:
            sse_clients.append(self.wfile)
        
        try:
            while True:
                if reload_event.wait(timeout=30):
                    self.wfile.write(b'data: reload\n\n')
                    self.wfile.flush()
                    reload_event.clear()
                else:
                    self.wfile.write(b': ping\n\n')
                    self.wfile.flush()
        except:
            pass
        finally:
            with sse_lock:
                if self.wfile in sse_clients:
                    sse_clients.remove(self.wfile)


def notify_reload():
    reload_event.set()


def get_mtimes(project_dir):
    """Get modification times for all watched files."""
    mtimes = {}
    project_path = Path(project_dir)
    
    # Watch .coi files in src/
    src_dir = project_path / 'src'
    if src_dir.exists():
        for f in src_dir.rglob('*.coi'):
            try:
                mtimes[str(f)] = f.stat().st_mtime
            except OSError:
                pass
    
    # Watch assets/
    assets_dir = project_path / 'assets'
    if assets_dir.exists():
        for f in assets_dir.rglob('*'):
            if f.is_file():
                try:
                    mtimes[str(f)] = f.stat().st_mtime
                except OSError:
                    pass
    
    # Watch CSS files in styles/
    styles_dir = project_path / 'styles'
    if styles_dir.exists():
        for f in styles_dir.rglob('*.css'):
            try:
                mtimes[str(f)] = f.stat().st_mtime
            except OSError:
                pass
    
    return mtimes


def watch_files(project_dir, coi_bin, keep_cc, cc_only, verbose):
    print(f'{DIM}  watching for changes{RESET}')
    last = get_mtimes(project_dir)
    
    while True:
        time.sleep(0.3)
        curr = get_mtimes(project_dir)
        
        changed = [Path(p).name for p, t in curr.items() if p not in last or last[p] != t]
        changed += [f'{Path(p).name} (deleted)' for p in last if p not in curr]
        
        if changed:
            print(f'  {YELLOW}↻{RESET} {DIM}{", ".join(changed)}{RESET}')
            
            # Use 'coi build' to ensure assets and styles/ CSS are bundled
            cmd = [coi_bin, 'build', '--rebuild']
            if keep_cc: cmd.append('--keep-cc')
            if cc_only: cmd.append('--cc-only')
            if verbose: cmd.append('--verbose')
            
            # the build draws its own status line and result, straight to the terminal
            try:
                r = subprocess.run(cmd, timeout=600, cwd=project_dir)
                if r.returncode == 0:
                    notify_reload()
            except Exception as e:
                print(f'  {RED}✗{RESET} {e}')
            
            last = curr


def main():
    global hot_reload_enabled
    
    if len(sys.argv) < 3:
        print('Usage: dev_server.py <project_dir> <coi_bin> [--no-watch] [--keep-cc] [--cc-only]')
        sys.exit(1)
    
    project_dir = sys.argv[1]
    coi_bin = sys.argv[2]
    
    hot_reload_enabled = '--no-watch' not in sys.argv
    keep_cc = '--keep-cc' in sys.argv
    cc_only = '--cc-only' in sys.argv
    verbose = '--verbose' in sys.argv
    
    os.chdir(os.path.join(project_dir, 'dist'))
    
    class Server(http.server.ThreadingHTTPServer):
        allow_reuse_address = True
    
    # 8000, or the next free port when something (another coi dev?) has it
    httpd = None
    for port in range(8000, 8011):
        try:
            httpd = Server(('', port), DevHandler)
            break
        except OSError:
            continue
    if httpd is None:
        print(f'  {RED}✗{RESET} ports 8000 to 8010 are all in use')
        sys.exit(1)
    print(f'  {GREEN}➜{RESET}  Local:   \033[36m\033[1mhttp://localhost:{port}{RESET}' + (f'  {DIM}(8000 was taken){RESET}' if port != 8000 else ''))
    if not hot_reload_enabled:
        print(f'  {DIM}↻ hot reload off{RESET}')
    print(f'  {DIM}press Ctrl+C to stop{RESET}')
    print()
    
    if hot_reload_enabled:
        watcher = threading.Thread(
            target=watch_files,
            args=(project_dir, coi_bin, keep_cc, cc_only, verbose),
            daemon=True
        )
        watcher.start()
    
    with httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()   # Ctrl+C: a clean line, no traceback


if __name__ == '__main__':
    main()
