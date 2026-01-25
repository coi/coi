#!/usr/bin/env python3
"""
Unified Benchmark Runner for Coi, React, and Vue.

Runs two benchmark suites:
1. Bundle Size - Uses counter apps (minimal implementation for clean size comparison)
2. DOM Performance - Uses rows apps (stress test with 1000 rows)
"""

import os
import json
import subprocess
import sys
import asyncio
import statistics

# Check for playwright (only needed for DOM benchmarks)
PLAYWRIGHT_AVAILABLE = False
try:
    from playwright.async_api import async_playwright
    PLAYWRIGHT_AVAILABLE = True
except ImportError:
    pass

FRAMEWORKS = ['coi', 'react', 'vue', 'svelte']
BENCHMARK_DIR = os.path.dirname(os.path.abspath(__file__))

# DOM benchmark operations
DOM_BENCHMARKS = [
    ('create1000', 'Create 1,000 rows'),
    ('clear', 'Clear rows'),
    ('create1000', 'Create 1,000 rows (2nd)'),
    ('update', 'Update 1,000 rows'),
    ('swap', 'Swap rows'),
    ('clear', 'Clear rows (2nd)'),
]

WARMUP_RUNS = 5
BENCHMARK_RUNS = 5

COLORS = {"coi": "#9477ff", "react": "#00d8ff", "vue": "#42b883", "svelte": "#e97d30"}


def parse_browser(ua: str) -> str:
    """Parse browser name and version from user agent string."""
    if 'Chrome' in ua:
        parts = ua.split('Chrome/')
        if len(parts) > 1:
            ver = parts[1].split(' ')[0]
            return f"Chrome {ver}"
    if 'Firefox' in ua:
        parts = ua.split('Firefox/')
        if len(parts) > 1:
            ver = parts[1].split(' ')[0]
            return f"Firefox {ver}"
    if 'Safari' in ua and 'Version' in ua:
        parts = ua.split('Version/')
        if len(parts) > 1:
            ver = parts[1].split(' ')[0]
            return f"Safari {ver}"
    return "Unknown Browser"


def print_header(text: str):
    """Print a formatted header."""
    print(f"\n{'=' * 60}")
    print(f"  {text}")
    print('=' * 60)


def build_counter_apps():
    """Build counter apps for bundle size measurement."""
    print("\nBuilding Counter Apps (for bundle size)...")
    
    # Build Coi counter
    coi_dir = os.path.join(BENCHMARK_DIR, 'coi-counter')
    if os.path.exists(coi_dir):
        try:
            subprocess.check_call(['coi', 'build'], cwd=coi_dir, 
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("  ✓ Coi counter built")
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"  ✗ Coi counter failed: {e}")
    
    # Build React, Vue, and Svelte counters
    for fw in ['react', 'vue', 'svelte']:
        fw_dir = os.path.join(BENCHMARK_DIR, f'{fw}-counter')
        if os.path.exists(fw_dir):
            try:
                subprocess.check_call(['npm', 'install'], cwd=fw_dir,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                subprocess.check_call(['npm', 'run', 'build'], cwd=fw_dir,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                print(f"  ✓ {fw.capitalize()} counter built")
            except subprocess.CalledProcessError as e:
                print(f"  ✗ {fw.capitalize()} counter failed: {e}")


def build_rows_apps():
    """Build rows apps for DOM performance measurement."""
    print("\nBuilding Rows Apps (for DOM performance)...")
    
    # Build Coi rows
    coi_dir = os.path.join(BENCHMARK_DIR, 'coi-rows')
    if os.path.exists(coi_dir):
        try:
            subprocess.check_call(['coi', 'build'], cwd=coi_dir,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("  ✓ Coi rows built")
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"  ✗ Coi rows failed: {e}")
    
    # Build React, Vue, and Svelte rows
    for fw in ['react', 'vue', 'svelte']:
        fw_dir = os.path.join(BENCHMARK_DIR, f'{fw}-rows')
        if os.path.exists(fw_dir):
            try:
                subprocess.check_call(['npm', 'install'], cwd=fw_dir,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                subprocess.check_call(['npm', 'run', 'build'], cwd=fw_dir,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                print(f"  ✓ {fw.capitalize()} rows built")
            except subprocess.CalledProcessError as e:
                print(f"  ✗ {fw.capitalize()} rows failed: {e}")


def get_dir_size(path: str) -> int:
    """Get total size of a directory in bytes."""
    total = 0
    if os.path.exists(path):
        for dirpath, _, filenames in os.walk(path):
            for f in filenames:
                fp = os.path.join(dirpath, f)
                if not os.path.islink(fp):
                    total += os.path.getsize(fp)
    return total


def measure_bundle_sizes() -> dict:
    """Measure bundle sizes using counter apps."""
    sizes = {}
    for fw in FRAMEWORKS:
        dist_path = os.path.join(BENCHMARK_DIR, f'{fw}-counter', 'dist')
        sizes[fw] = get_dir_size(dist_path)
    return sizes


async def run_dom_benchmark(page, button_id: str) -> float:
    """Run a single DOM benchmark operation using double-rAF timing (same as Svelte)."""
    result = await page.evaluate(r'''(buttonId) => {
        return new Promise((resolve) => {
            const button = document.getElementById(buttonId);
            if (!button) {
                resolve(-1);
                return;
            }
            
            const startTime = performance.now();
            button.click();
            
            // Double-rAF: wait for DOM update AND paint to complete
            requestAnimationFrame(() => {
                requestAnimationFrame(() => {
                    const duration = performance.now() - startTime;
                    
                    // Debug: count rendered rows
                    const rowCount = document.querySelectorAll('.row').length;
                    console.log(`Rendered rows: ${rowCount}, Duration: ${duration}ms`);
                    
                    resolve(duration);
                });
            });
        });
    }''', button_id)
    await asyncio.sleep(0.1)
    return result

async def benchmark_framework_dom(browser, framework: str, port: int) -> dict:
    """Run DOM benchmarks for a single framework."""
    results = {}
    page = await browser.new_page()
    await page.goto(f'http://localhost:{port}')
    await page.wait_for_selector('.controls')
    
    # Wait 2 seconds for page to fully load and stabilize
    print(f"      Waiting 2s for page to stabilize...", flush=True)
    await asyncio.sleep(2.0)
    
    # Warmup runs - give JIT time to optimize
    for _ in range(WARMUP_RUNS):
        await run_dom_benchmark(page, 'create1000')
        await asyncio.sleep(0.3)
        await run_dom_benchmark(page, 'clear')
        await asyncio.sleep(0.3)
    
    # Run benchmarks
    for button_id, label in DOM_BENCHMARKS:
        durations = []
        for _ in range(BENCHMARK_RUNS):
            # Clear before each create to ensure consistent starting state
            if button_id == 'create1000':
                await run_dom_benchmark(page, 'clear')
                await asyncio.sleep(0.1)
            duration = await run_dom_benchmark(page, button_id)
            if duration > 0:
                durations.append(duration)
        
        if durations:
            results[label] = {
                'mean': statistics.mean(durations),
                'median': statistics.median(durations),
                'min': min(durations),
                'max': max(durations),
                'stddev': statistics.stdev(durations) if len(durations) > 1 else 0,
            }
        else:
            results[label] = {'mean': -1, 'error': 'No valid results'}
    
    await page.close()
    return results


async def run_dom_benchmarks() -> tuple:
    """Run DOM performance benchmarks using rows apps."""
    if not PLAYWRIGHT_AVAILABLE:
        print("  ⚠ Playwright not available, skipping DOM benchmarks")
        print("    Install with: pip install playwright && playwright install chromium")
        return {}, ""
    
    all_results = {}
    servers = {}
    
    # Start servers
    print("\n  Starting servers...")
    base_port = 5173
    for i, fw in enumerate(FRAMEWORKS):
        port = base_port + i
        fw_dir = os.path.join(BENCHMARK_DIR, f'{fw}-rows', 'dist')
        
        if not os.path.exists(fw_dir):
            print(f"    ✗ {fw.capitalize()} dist not found")
            continue
        
        process = subprocess.Popen(
            [sys.executable, '-m', 'http.server', str(port)],
            cwd=fw_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        servers[fw] = {'process': process, 'port': port}
        print(f"    ✓ {fw.capitalize()} on port {port}")
    
    await asyncio.sleep(2)
    
    # Run benchmarks
    print("\n  Running benchmarks...")
    print("  Waiting 2 seconds for everything to be ready...", flush=True)
    await asyncio.sleep(2)
    
    browser_name = "Unknown Browser"
    async with async_playwright() as p:
        # Use visible browser - headless throttles requestAnimationFrame
        browser = await p.chromium.launch(headless=False)
        
        for fw in FRAMEWORKS:
            if fw not in servers:
                continue
            
            port = servers[fw]['port']
            print(f"    {fw.capitalize()} (port {port})...", flush=True)
            try:
                page = await browser.new_page()
                await page.goto(f'http://localhost:{port}')
                
                # Get browser version from first page
                if browser_name == "Unknown Browser":
                    ua = await page.evaluate('navigator.userAgent')
                    browser_name = parse_browser(ua)
                    print(f"  Browser: {browser_name}")
                
                await page.close()
                results = await benchmark_framework_dom(browser, fw, port)
                all_results[fw] = results
                # Print key results immediately for verification
                create_time = results.get('Create 1,000 rows', {}).get('mean', -1)
                print(f"      -> Create 1000: {create_time:.1f}ms ✓")
            except Exception as e:
                print(f"      ✗ ({e})")
                all_results[fw] = {'error': str(e)}
        
        await browser.close()
    
    # Stop servers
    for fw, server in servers.items():
        server['process'].terminate()
    
    return all_results, browser_name


def print_bundle_results(sizes: dict):
    """Print bundle size results."""
    print_header("BUNDLE SIZE (Counter App)")
    print(f"{'Framework':<15} | {'Size':>12}")
    print("-" * 30)
    
    sorted_sizes = sorted(sizes.items(), key=lambda x: x[1])
    winner = sorted_sizes[0][0] if sorted_sizes else None
    
    for fw, size in sorted_sizes:
        kb = size / 1024
        star = " ★" if fw == winner else ""
        print(f"{fw.capitalize():<15} | {kb:>10.1f}KB{star}")


def print_dom_results(results: dict):
    """Print DOM benchmark results."""
    if not results:
        return
    
    print_header("DOM PERFORMANCE (Rows App)")
    
    benchmarks = ['Create 1,000 rows', 'Update 1,000 rows', 'Swap rows', 'Clear rows']
    
    # Header
    header = f"{'Benchmark':<22}"
    for fw in FRAMEWORKS:
        header += f" | {fw.capitalize():>10}"
    print(header)
    print("-" * 75)
    
    # Results
    for bench in benchmarks:
        row = f"{bench:<22}"
        # Collect values
        vals = {}
        for fw in FRAMEWORKS:
            if fw in results and bench in results[fw]:
                val = results[fw][bench].get('mean', -1)
                if val > 0:
                    vals[fw] = val
        
        # Find minimum value for winner detection
        min_val = min(vals.values()) if vals else 0
        
        # Print in framework order (matching header)
        for fw in FRAMEWORKS:
            val = vals.get(fw, -1)
            if val > 0:
                is_winner = (val == min_val)
                star = " ★" if is_winner else ""
                row += f" | {val:>7.1f}ms{star}"
            else:
                row += " |       N/A"
        print(row)
    
    print("-" * 60)


def generate_svg_report(bundle_sizes: dict, dom_results: dict, browser_name: str = ""):
    """Generate combined SVG visualization."""
    has_dom = bool(dom_results)
    width = 800
    height = 750 if has_dom else 280
    
    bg_color, text_main, text_sub = "#f8f9fa", "#212529", "#6c757d"
    
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="100%" style="font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, sans-serif; background: {bg_color};">']
    svg.append(f'<rect width="100%" height="100%" fill="{bg_color}"/>')
    
    # Title
    svg.append(f'<text x="{width/2}" y="35" text-anchor="middle" fill="{text_main}" font-size="22" font-weight="bold">Coi vs React vs Vue vs Svelte Benchmark</text>')
    
    # Browser info subtitle (if available)
    if browser_name:
        svg.append(f'<text x="{width/2}" y="52" text-anchor="middle" fill="{text_sub}" font-size="11">{browser_name}</text>')
    
    # Legend - centered properly (4 items: ~320px total width)
    legend_y = 60
    legend_start = width/2 - 160
    for i, fw in enumerate(FRAMEWORKS):
        lx = legend_start + i * 85
        svg.append(f'<rect x="{lx}" y="{legend_y}" width="14" height="14" fill="{COLORS[fw]}" rx="3"/>')
        svg.append(f'<text x="{lx + 20}" y="{legend_y + 12}" fill="{text_main}" font-size="13" font-weight="600">{fw.capitalize()}</text>')
    
    y = 105
    chart_x = 180
    max_bar_width = 450
    
    # Bundle Size Section
    svg.append(f'<text x="{width/2}" y="{y}" text-anchor="middle" fill="{text_main}" font-size="16" font-weight="bold">Bundle Size</text>')
    svg.append(f'<text x="{width/2}" y="{y + 18}" text-anchor="middle" fill="{text_sub}" font-size="12">Counter App - Smaller is better (KB)</text>')
    
    y += 45
    sorted_sizes = sorted(bundle_sizes.items(), key=lambda x: x[1])
    max_size = max(bundle_sizes.values()) if bundle_sizes.values() else 1
    min_size = min(s for s in bundle_sizes.values() if s > 0) if bundle_sizes.values() else 0
    
    for fw, size in sorted_sizes:
        kb = size / 1024
        bar_w = (size / max_size) * max_bar_width
        is_winner = size == min_size
        
        svg.append(f'<text x="{chart_x - 12}" y="{y + 18}" text-anchor="end" fill="{text_main}" font-size="13" font-weight="bold">{fw.capitalize()}</text>')
        svg.append(f'<rect x="{chart_x}" y="{y}" width="{bar_w}" height="26" fill="{COLORS[fw]}" rx="4"/>')
        
        weight = "bold" if is_winner else "normal"
        star = " ★" if is_winner else ""
        svg.append(f'<text x="{chart_x + bar_w + 8}" y="{y + 18}" fill="{text_main}" font-size="13" font-weight="{weight}">{kb:.1f} KB{star}</text>')
        y += 36
    
    # DOM Performance Section (if available)
    if has_dom:
        y += 30
        svg.append(f'<text x="{width/2}" y="{y}" text-anchor="middle" fill="{text_main}" font-size="16" font-weight="bold">DOM Performance</text>')
        svg.append(f'<text x="{width/2}" y="{y + 18}" text-anchor="middle" fill="{text_sub}" font-size="12">Rows App - Lower is better (ms)</text>')
        
        y += 45
        benchmarks = ['Create 1,000 rows', 'Update 1,000 rows', 'Swap rows', 'Clear rows']
        
        # Find max value for scaling
        max_val = 1
        for fw in FRAMEWORKS:
            if fw in dom_results:
                for bench in benchmarks:
                    if bench in dom_results[fw]:
                        val = dom_results[fw][bench].get('mean', 0)
                        if val > max_val:
                            max_val = val
        
        bar_height = 18
        bar_gap = 4
        group_gap = 25
        max_bar_width = 380
        
        for bench in benchmarks:
            # Benchmark label
            svg.append(f'<text x="{chart_x - 12}" y="{y + bar_height + 6}" text-anchor="end" fill="{text_main}" font-size="12" font-weight="bold">{bench}</text>')
            
            # Get values and find winner, sorted fastest first
            bench_vals = []
            for fw in FRAMEWORKS:
                if fw in dom_results and bench in dom_results[fw]:
                    v = dom_results[fw][bench].get('mean', 0)
                    if v > 0:
                        bench_vals.append((fw, v))
            
            bench_vals.sort(key=lambda x: x[1])  # Sort by time, fastest first
            min_val = bench_vals[0][1] if bench_vals else 0
            
            # Draw bars for each framework (sorted)
            bar_y = y
            for fw, val in bench_vals:
                bar_w = (val / max_val) * max_bar_width
                is_winner = (val == min_val)
                
                svg.append(f'<rect x="{chart_x}" y="{bar_y}" width="{bar_w}" height="{bar_height}" fill="{COLORS[fw]}" rx="3"/>')
                
                # Value label
                star = " ★" if is_winner else ""
                weight = "bold" if is_winner else "normal"
                svg.append(f'<text x="{chart_x + bar_w + 8}" y="{bar_y + 13}" fill="{text_main}" font-size="11" font-weight="{weight}">{val:.1f}ms{star}</text>')
                
                bar_y += bar_height + bar_gap
            
            y += (bar_height + bar_gap) * 3 + group_gap
    
    svg.append('</svg>')
    
    output_path = os.path.join(BENCHMARK_DIR, 'benchmark_results.svg')
    with open(output_path, 'w') as f:
        f.write('\n'.join(svg))
    print(f"\n✓ SVG report: {output_path}")


def main():
    skip_build = "--no-build" in sys.argv
    size_only = "--size-only" in sys.argv
    dom_only = "--dom-only" in sys.argv
    
    print_header("COI BENCHMARK SUITE")
    
    # Build projects
    if not skip_build:
        if not dom_only:
            build_counter_apps()
        if not size_only:
            build_rows_apps()
    
    # Measure bundle sizes
    bundle_sizes = {}
    if not dom_only:
        bundle_sizes = measure_bundle_sizes()
        print_bundle_results(bundle_sizes)
    
    # Run DOM benchmarks
    dom_results = {}
    browser_name = ""
    if not size_only:
        print_header("RUNNING DOM BENCHMARKS")
        dom_results, browser_name = asyncio.run(run_dom_benchmarks())
        print_dom_results(dom_results)
    
    # Generate reports
    generate_svg_report(bundle_sizes, dom_results, browser_name)
    
    # Save JSON
    output = {
        'browser': browser_name,
        'bundle_sizes': bundle_sizes,
        'dom_performance': dom_results
    }
    json_path = os.path.join(BENCHMARK_DIR, 'benchmark_results.json')
    with open(json_path, 'w') as f:
        json.dump(output, f, indent=2)
    print(f"✓ JSON results: {json_path}")


if __name__ == "__main__":
    main()
