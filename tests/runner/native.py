
import re
import subprocess
import sys
from pathlib import Path
from .base import TestRunnerBase, GREEN, RED, NC

class NativeRunner(TestRunnerBase):
    """tests/native/*.coi are run with `coi test`. A file must build and pass every test block in
    it. A file with a `name.expected` beside it must instead print each of its lines (a compile
    error, or a failed check with both sides): that is how the runner's own output is checked."""

    def run(self, tests_dir):
        self.ensure_build()
        native_dir = Path(tests_dir).resolve() / "native"
        files = sorted(p for p in native_dir.glob("*.coi") if p.is_file())   # .coi/ is the cache
        if not files:
            print("No native tests found.")
            return

        print("Running native tests...")
        failures = []
        for i, f in enumerate(files):
            result = subprocess.run([str(self.compiler_bin), "test", str(f)], cwd=native_dir,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            out = re.sub(r"\x1b\[[0-9;]*m", "", result.stdout)
            expected = f.with_suffix(".expected")
            if expected.exists():
                wanted = [l.strip() for l in expected.read_text().splitlines() if l.strip()]
                missing = [l for l in wanted if l not in out]
                if missing or result.returncode == 0:
                    failures.append(f"{f.name} (exit {result.returncode}; not in the output: " + " | ".join(missing)[:200] + ")")
            elif result.returncode != 0 or "passed" not in out:
                tail = " | ".join(l.strip() for l in out.strip().splitlines()[-4:])
                failures.append(f"{f.name} (expected every test to pass; exit {result.returncode}: {tail[:200]})")
            self.draw_progress_bar(i + 1, len(files))
        print("")

        if not failures:
            print(f"{GREEN}All {len(files)} native tests passed!{NC}")
        else:
            print(f"{RED}{len(failures)} native test(s) failed:{NC}")
            for msg in failures:
                print(f"  {RED}✗{NC} {msg}")
            sys.exit(1)
