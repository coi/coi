
import os
import sys
import shutil
import tempfile
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from .base import TestRunnerBase, GREEN, RED, NC

class UnitRunner(TestRunnerBase):
    def build_pass_tests(self, tests, tests_dir):
        """Build each _pass test all the way to WASM: generating C++ alone accepts programs
        the C++ compiler rejects. Each gets its own --out (and so its own .coi/cache next to
        it), so they build in parallel while imports still resolve from the test's folder."""
        work = Path(tempfile.mkdtemp(prefix="coi-unit-"))
        failures = []

        def build(i_test):
            i, test = i_test
            out = work / str(i) / "out"
            result = subprocess.run([str(self.compiler_bin), str(test), "--out", str(out)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if result.returncode == 0:
                return None
            first = next((l for l in result.stdout.splitlines() if "error" in l.lower()), "").strip()
            return f"{test.relative_to(tests_dir)} (doesn't build: {first[:160]})"

        done = 0
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            for failure in pool.map(build, enumerate(tests)):
                done += 1
                self.draw_progress_bar(done, len(tests))
                if failure:
                    failures.append(failure)
        shutil.rmtree(work, ignore_errors=True)
        print("")
        return failures

    def run(self, tests_dir, fast=False):
        self.ensure_build()
        
        tests_dir = Path(tests_dir).resolve()
        test_files = []
        
        for root, _, files in os.walk(tests_dir):
            for file in files:
                if file.endswith("_pass.coi") or file.endswith("_fail.coi"):
                    test_files.append(Path(root) / file)
        
        total = len(test_files)
        if total == 0:
            print("No unit tests found.")
            return

        print("Running tests...")
        passed_count = 0
        failures = []
        
        for i, test_file in enumerate(sorted(test_files)):
            is_pass_test = test_file.name.endswith("_pass.coi")
            
            # Run compiler only (no linking/execution needed for these unit tests usually, based on run_unit.sh --cc-only)
            cmd = [str(self.compiler_bin), str(test_file), "--cc-only"]
            
            # Capture output to avoid clutter
            result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            
            success = False
            if is_pass_test:
                if result.returncode == 0:
                    success = True
                else:
                    failures.append(f"{test_file.relative_to(tests_dir)} (expected success, got failure)")
            else: # fail test
                if result.returncode != 0:
                    success = True
                else:
                    failures.append(f"{test_file.relative_to(tests_dir)} (expected failure, got success)")
            
            if success:
                passed_count += 1
                
            self.draw_progress_bar(i + 1, total)
            
            # Cleanup generated files (similar to run_unit.sh)
            # It seems run_unit.sh removes .cc files and app.cc?
            # Let's clean up potential artifacts
            cc_file = test_file.with_suffix(".cc")
            if cc_file.exists():
                cc_file.unlink()
            app_cc = test_file.parent / "app.cc" # Standard output sometimes?
            if app_cc.exists():
                app_cc.unlink()

        print("") # Newline after progress bar

        if not fast:
            pass_tests = [t for t in sorted(test_files) if t.name.endswith("_pass.coi")
                          and not any(str(t.relative_to(tests_dir)) in f for f in failures)]
            print(f"Building {len(pass_tests)} pass tests to WASM...")
            build_failures = self.build_pass_tests(pass_tests, tests_dir)
            failures += build_failures
            passed_count -= len(build_failures)

        if len(failures) == 0:
            print(f"{GREEN}All {total} tests passed!{NC}")
        else:
            print(f"{RED}{len(failures)} test(s) failed:{NC}")
            for fail_msg in failures:
                print(f"  {RED}✗{NC} {fail_msg}")
            print(f"\n{GREEN}{passed_count} passed{NC}, {RED}{len(failures)} failed{NC} out of {total} tests")
            sys.exit(1)
