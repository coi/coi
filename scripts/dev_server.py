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
DIM = '\033[2m'
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
        
        path = self.translate_path(self.path)
        
        if os.path.isfile(path):
            if path.endswith('.html') and hot_reload_enabled:
                self.serve_html_with_reload(path)
            else:
                super().do_GET()
            return
        
        # SPA fallback
        self.path = '/index.html'
        index_path = self.translate_path(self.path)
        if os.path.isfile(index_path):
            if hot_reload_enabled:
                self.serve_html_with_reload(index_path)
            else:
                super().do_GET()
        else:
            self.send_error(404)
    
    def serve_html_with_reload(self, path):
        """Inject hot reload script into HTML."""
        try:
            with open(path, 'rb') as f:
                content = f.read()
            
            script = b'''<script>(function(){var e=new EventSource('/__hot_reload');e.onmessage=function(m){if(m.data==='reload')location.reload()};e.onerror=function(){console.log('[Coi] Reconnecting...')}})();</script></body>'''
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


def watch_files(project_dir, coi_bin, keep_cc, cc_only):
    print(f'{DIM}  Watching for changes...{RESET}')
    last = get_mtimes(project_dir)
    
    while True:
        time.sleep(0.3)
        curr = get_mtimes(project_dir)
        
        changed = [Path(p).name for p, t in curr.items() if p not in last or last[p] != t]
        changed += [f'{Path(p).name} (deleted)' for p in last if p not in curr]
        
        if changed:
            print(f'{YELLOW}↻{RESET} {DIM}{", ".join(changed)}{RESET}')
            
            # Use 'coi build' to ensure assets and styles/ CSS are bundled
            cmd = [coi_bin, 'build']
            if keep_cc: cmd.append('--keep-cc')
            if cc_only: cmd.append('--cc-only')
            
            try:
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=30, cwd=project_dir)
                if r.returncode == 0:
                    print(f'{GREEN}✓{RESET} Rebuilt')
                    notify_reload()
                else:
                    print(f'{RED}✗{RESET} Build failed:')
                    output = r.stdout + r.stderr
                    for line in output.splitlines():
                        if line.strip():
                            print(f'  {line}')
            except Exception as e:
                print(f'{RED}✗{RESET} {e}')
            
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
    
    os.chdir(os.path.join(project_dir, 'dist'))
    
    if hot_reload_enabled:
        watcher = threading.Thread(
            target=watch_files,
            args=(project_dir, coi_bin, keep_cc, cc_only),
            daemon=True
        )
        watcher.start()
    
    class Server(http.server.ThreadingHTTPServer):
        allow_reuse_address = True
    
    with Server(('', 8000), DevHandler) as httpd:
        httpd.serve_forever()


if __name__ == '__main__':
    main()
