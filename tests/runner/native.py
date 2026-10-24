
import subprocess
from pathlib import Path
from .base import TestRunnerBase, GREEN, RED, NC

class NativeRunner(TestRunnerBase):
    """tests/native/*.coi are run with `coi test`. A file must build and pass every test block in
    it, unless its name says otherwise: *_fails.coi must build and have a test fail (exit code 1
    and a failed check in the output), *_rejects.coi must not build (a compile error)."""

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
            out = result.stdout
            if f.name.endswith("_fails.coi"):
                ok = result.returncode == 1 and "failed" in out and "expect(" in out
                why = "expected a failing test"
            elif f.name.endswith("_rejects.coi"):
                ok = result.returncode != 0 and "Error:" in out and ">> tests" not in out and "failed in" not in out and " passed" not in out
                why = "expected a compile error"
            else:
                ok = result.returncode == 0 and "passed" in out
                why = "expected every test to pass"
            if not ok:
                tail = " | ".join(l.strip() for l in out.strip().splitlines()[-4:])
                failures.append(f"{f.name} ({why}; exit {result.returncode}: {tail[:200]})")
            self.draw_progress_bar(i + 1, len(files))
        print("")

        if not failures:
            print(f"{GREEN}All {len(files)} native tests passed!{NC}")
        else:
            print(f"{RED}{len(failures)} native test(s) failed:{NC}")
            for msg in failures:
                print(f"  {RED}✗{NC} {msg}")
            import sys
            sys.exit(1)
