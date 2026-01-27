
import os
import sys
import subprocess
from pathlib import Path
from .base import TestRunnerBase, GREEN, RED, NC

class UnitRunner(TestRunnerBase):
    def run(self, tests_dir):
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
        
        if len(failures) == 0:
            print(f"{GREEN}All {total} tests passed!{NC}")
        else:
            print(f"{RED}{len(failures)} test(s) failed:{NC}")
            for fail_msg in failures:
                print(f"  {RED}✗{NC} {fail_msg}")
            print(f"\n{GREEN}{passed_count} passed{NC}, {RED}{len(failures)} failed{NC} out of {total} tests")
            sys.exit(1)
