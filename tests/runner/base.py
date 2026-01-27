
import sys
import os
import subprocess
from pathlib import Path

# Colors
GREEN = "\033[0;32m"
RED = "\033[0;31m"
PURPLE = "\033[38;2;148;119;255m"
NC = "\033[0m"

class TestRunnerBase:
    def __init__(self, root_dir):
        self.root_dir = Path(root_dir).resolve()
        self.compiler_bin = self.root_dir / "coi"

    def print_step(self, msg):
        print(f"{PURPLE}==>{NC} {msg}")

    def fail(self, msg):
        print(f"{RED}error:{NC} {msg}", file=sys.stderr)
        sys.exit(1)

    def ensure_build(self):
        if not self.compiler_bin.exists() or not os.access(self.compiler_bin, os.X_OK):
            self.print_step("Compiler not found. Building...")
            subprocess.check_call(["./build.sh"], cwd=self.root_dir)
        
        if not self.compiler_bin.exists():
            self.fail(f"Expected executable at {self.compiler_bin}")

    def draw_progress_bar(self, current, total, width=50):
        filled = int(current * width / total)
        bar = f"{PURPLE}█{NC}" * filled + "░" * (width - filled)
        print(f"\r[{bar}] {current}/{total}", end="", flush=True)
