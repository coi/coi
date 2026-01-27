
import shutil
import subprocess
from .web_base import WebRunnerBase, WebServer

class GalleryRunner(WebRunnerBase):
    def __init__(self, root_dir):
        super().__init__(root_dir)
        self.visual_dir = self.web_dir / ".cache/visual"

    def run(self, args):
        self.check_deps()
        self.ensure_build()
        
        scenes = self.parse_manifest(args.scene)
        if not scenes:
            self.fail("No scenes matched")

        output_dir = self.visual_dir
        if args.out:
            from pathlib import Path
            output_dir = Path(args.out)

        output_dir.mkdir(parents=True, exist_ok=True)
        
        # Init index.html
        index_path = output_dir / "index.html"
        index_content = ["""<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>COI Web Visual Gallery</title>
  <style>
    body{margin:0;padding:24px;font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#0f1117;color:#e8e8e8}
    .grid{display:grid;grid-template-columns:1fr;gap:18px}
    .card{border:1px solid rgba(255,255,255,0.10);border-radius:12px;padding:14px;background:rgba(255,255,255,0.04)}
    h2{margin:0 0 10px 0;font-size:16px;font-weight:600}
    img{max-width:100%;height:auto;border-radius:10px;border:1px solid rgba(255,255,255,0.08);background:#000}
    .path{opacity:.7;font-size:12px}
  </style>
</head>
<body>
  <h1 style="margin:0 0 18px 0">COI Web Visual Gallery</h1>
  <div class="grid">
"""]

        browser = self.get_browser(args.browser)

        for name, rel_path in scenes:
            self.print_step(f"web visual: {name}")
            scene_path = self.root_dir / rel_path
            if not scene_path.exists():
                print(f"warn: missing scene file: {scene_path}")
                continue

            scene_out_dir = output_dir / name
            build_dir = scene_out_dir / "build"
            if build_dir.exists():
                shutil.rmtree(build_dir)
            build_dir.mkdir(parents=True, exist_ok=True)

            # Build
            subprocess.check_call([str(self.compiler_bin), str(scene_path), "--out", str(build_dir)], stdout=subprocess.DEVNULL)
            (build_dir / "favicon.ico").touch()

            # Serve and Capture
            with WebServer(build_dir) as url:
                 full_url = f"{url}/index.html"
                 png_path = scene_out_dir / "capture.png"
                 
                 cmd = [
                     "node", str(self.web_dir / "web_capture_playwright.mjs"),
                     "--url", full_url,
                     "--out", str(png_path),
                     "--size", args.size,
                     "--timeout-ms", "12000",
                     "--browser", browser
                 ]
                 subprocess.check_call(cmd)

            index_content.append(f"""
    <div class="card">
      <h2>{name}</h2>
      <div class="path">{rel_path}</div>
      <div style="height:10px"></div>
      <img src="{name}/capture.png" />
    </div>
""")

        index_content.append("""  </div>\n</body>\n</html>""")
        with open(index_path, "w") as f:
            f.write("".join(index_content))

        print(f"Gallery: {index_path}")
        if args.open:
            import webbrowser
            webbrowser.open(f"file://{index_path}")
