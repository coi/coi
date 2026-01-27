
import sys
import shutil
import subprocess
from .base import GREEN, RED, NC
from .web_base import WebRunnerBase, WebServer

class IntegrationRunner(WebRunnerBase):
    def __init__(self, root_dir):
        super().__init__(root_dir)
        self.out_dir = self.web_dir / ".cache/integration"
    
    def run(self, args):
        self.check_deps()
        self.ensure_build()
        
        scenes = self.parse_manifest(args.scene)
        if not scenes:
            self.fail("No scenes matched")

        self.out_dir.mkdir(parents=True, exist_ok=True)
        browser = self.get_browser(args.browser)

        failed = 0
        total = len(scenes)

        for i, (name, rel_path) in enumerate(scenes):
            scene_path = self.root_dir / rel_path
            if not scene_path.exists():
                print(f"error: missing scene file: {scene_path}")
                failed += 1
                continue

            print(f"[{i+1}/{total}] {name}...", end="", flush=True)

            scene_out_dir = self.out_dir / name
            build_dir = scene_out_dir / "build"
            if scene_out_dir.exists():
                shutil.rmtree(scene_out_dir)
            build_dir.mkdir(parents=True, exist_ok=True)

            try:
                # Capture both stdout and stderr to prevent noise
                subprocess.check_output(
                    [str(self.compiler_bin), str(scene_path), "--out", str(build_dir)], 
                    stderr=subprocess.STDOUT
                )
            except subprocess.CalledProcessError as e:
                print(f" {RED}FAIL (Build){NC}")
                print(e.output.decode('utf-8'))
                failed += 1
                continue
                
            (build_dir / "favicon.ico").touch()
            test_file = scene_path.with_suffix(".web_test.mjs")
            
            with WebServer(build_dir) as url:
                full_url = f"{url}/index.html"
                screenshot_fail = scene_out_dir / "fail.png"

                cmd = [
                    "node", str(self.web_dir / "web_integration_playwright.mjs"),
                    "--url", full_url,
                    "--size", args.size,
                    "--timeout-ms", "12000",
                    "--browser", browser,
                    "--screenshot", str(screenshot_fail)
                ]
                if args.headed:
                    cmd.append("--headed")
                if test_file.exists():
                    cmd.extend(["--test", str(test_file)])

                try:
                    subprocess.run(
                        cmd, 
                        stdout=subprocess.DEVNULL, 
                        stderr=subprocess.DEVNULL,
                        check=True
                    )
                    # Clear the line and print OK
                    print(f"\r\033[K[{i+1}/{total}] {name} {GREEN}OK{NC}")
                except subprocess.CalledProcessError:
                    print(f"\r\033[K[{i+1}/{total}] {name} {RED}FAIL{NC} (see {screenshot_fail})")
                    failed += 1

        if failed == 0:
            print(f"\n{GREEN}All {total} tests passed!{NC}")
        else:
            print(f"\n{RED}{failed} test(s) failed out of {total}{NC}")
            sys.exit(1)
