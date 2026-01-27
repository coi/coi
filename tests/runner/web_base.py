
import os
import sys
import shutil
import subprocess
import socket
import time
from .base import TestRunnerBase

class WebServer:
    def __init__(self, root_dir):
        self.root_dir = root_dir
        self.proc = None
        self.port = self._get_free_port()

    def _get_free_port(self):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    def __enter__(self):
        cmd = [sys.executable, "-m", "http.server", str(self.port), "--bind", "127.0.0.1", "--directory", str(self.root_dir)]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        return f"http://127.0.0.1:{self.port}"

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.proc:
            self.proc.terminate()
            self.proc.wait()

class WebRunnerBase(TestRunnerBase):
    def __init__(self, root_dir):
        super().__init__(root_dir)
        self.web_dir = self.root_dir / "tests/integration/web"
        self.manifest_path = self.web_dir / "scenes_manifest.txt"
    
    def check_deps(self):
        if not (self.web_dir / "node_modules").exists():
            self.print_step("Installing web dependencies...")
            subprocess.check_call(["npm", "install"], cwd=self.web_dir)

    def get_browser(self, user_browser=None):
        browser = os.environ.get("WEB_BROWSER")
        if user_browser:
            browser = user_browser
        elif not browser:
             for candidate in ["google-chrome", "chromium", "chromium-browser"]:
                if shutil.which(candidate):
                    browser = shutil.which(candidate)
                    break
        if not browser:
            self.fail("Web browser not found. Set WEB_BROWSER or use --browser")
        return browser

    def parse_manifest(self, filter_name=None):
        scenes = []
        if not self.manifest_path.exists():
            self.fail(f"Manifest not found: {self.manifest_path}")
        
        with open(self.manifest_path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = [p.strip() for p in line.split("|")]
                if len(parts) < 2:
                    continue
                
                name, rel_path = parts[0], parts[1]
                backends = parts[2] if len(parts) > 2 else ""
                
                if "web" not in backends.split(","):
                    continue
                    
                if filter_name:
                    if filter_name.endswith("*"):
                        if not name.startswith(filter_name[:-1]):
                            continue
                    elif name != filter_name:
                        continue
                
                scenes.append((name, rel_path))
        return scenes
