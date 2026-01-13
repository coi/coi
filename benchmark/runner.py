import os
import json
import subprocess
import sys

# Framework configuration
FRAMEWORKS = ['coi', 'react', 'vue']

def build_projects():
    print("--- Building projects ---")
    
    # Check if coi is available
    if subprocess.call("command -v coi", shell=True, stdout=subprocess.DEVNULL) != 0:
        print("Error: 'coi' command not found.")
        sys.exit(1)

    # Build Coi
    print("Building Coi...")
    coi_dir = os.path.join(os.path.dirname(__file__), 'coi-counter')
    try:
        subprocess.check_call(['coi', 'build'], cwd=coi_dir)
    except subprocess.CalledProcessError:
        print("Failed to build Coi project")
    
    # Build React and Vue using NPM
    for fw in ['react', 'vue']:
        print(f"Building {fw.capitalize()}...")
        fw_dir = os.path.join(os.path.dirname(__file__), f'{fw}-counter')
        try:
            subprocess.check_call(['npm', 'install'], cwd=fw_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.check_call(['npm', 'run', 'build'], cwd=fw_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            print(f"Failed to build {fw} project")

def get_dir_size(start_path):
    total_size = 0
    if not os.path.exists(start_path):
        return 0
    for dirpath, dirnames, filenames in os.walk(start_path):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            if not os.path.islink(fp):
                total_size += os.path.getsize(fp)
    return total_size

def generate_svg_report(metrics):
    width, height = 800, 380 
    bg_color, text_main, text_sub = "#f8f9fa", "#212529", "#6c757d"
    colors = {"coi": "#9477ff", "react": "#00d8ff", "vue": "#42b883"}

    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="100%" style="font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, sans-serif; background: {bg_color};">']
    svg.append(f'<rect width="100%" height="100%" fill="{bg_color}"/>')
    
    # Updated Header & Description
    svg.append(f'<text x="{width/2}" y="45" text-anchor="middle" fill="{text_main}" font-size="26" font-weight="bold">Bundle Size Comparison</text>')
    svg.append(f'<text x="{width/2}" y="75" text-anchor="middle" fill="{text_sub}" font-size="16">Counter App Implementation</text>')
    
    # Legend
    lx = width/2 - 180
    legend_y = 115
    for fw in FRAMEWORKS:
        svg.append(f'<rect x="{lx}" y="{legend_y}" width="15" height="15" fill="{colors[fw]}" rx="3"/>')
        svg.append(f'<text x="{lx + 25}" y="{legend_y + 13}" fill="{text_main}" font-size="14" font-weight="600">{fw.capitalize()}</text>')
        lx += 120

    # Chart Logic
    chart_x, chart_y = 220, 180
    bar_h, gap = 32, 12
    max_w = 400
    
    # Sort frameworks by size (ascending)
    sorted_fws = sorted(FRAMEWORKS, key=lambda f: metrics[f]['bundle_size'])
    vals = [metrics[fw]['bundle_size'] / 1024 for fw in sorted_fws]
    max_val = max(vals) if max(vals) > 0 else 1
    min_val = min([v for v in vals if v > 0])
    
    results = []
    for i, fw in enumerate(sorted_fws):
        results.append({
            'name': fw.capitalize(),
            'val': metrics[fw]['bundle_size'] / 1024,
            'color': colors[fw],
            'off': (bar_h + gap) * i
        })

    for res in results:
        w = (res['val'] / max_val) * max_w
        is_winner = res['val'] == min_val
        y_pos = chart_y + res['off']
        
        # Label
        svg.append(f'<text x="{chart_x - 15}" y="{y_pos + 22}" text-anchor="end" fill="{text_main}" font-size="15" font-weight="bold">{res["name"]}</text>')
        # Bar
        svg.append(f'<rect x="{chart_x}" y="{y_pos}" width="{w}" height="{bar_h}" fill="{res["color"]}" rx="4"/>')
        # Value text
        weight = "bold" if is_winner else "normal"
        win_star = " ★" if is_winner else ""
        svg.append(f'<text x="{chart_x + w + 10}" y="{y_pos + 22}" fill="{text_main}" font-size="14" font-weight="{weight}">{res["val"]:.1f} KB{win_star}</text>')

    svg.append('</svg>')
    
    with open('benchmark_results.svg', 'w') as f:
        f.write('\n'.join(svg))

def main():
    if "--no-build" not in sys.argv:
        build_projects()

    # Calculate metrics
    metrics = {}
    for fw in FRAMEWORKS:
        dist_path = os.path.join(f'{fw}-counter', 'dist')
        metrics[fw] = {'bundle_size': get_dir_size(dist_path)}

    # Sort results by size
    sorted_metrics = dict(sorted(metrics.items(), key=lambda item: item[1]['bundle_size']))

    # Terminal Output
    print("\n=== BUNDLE SIZE RESULTS ===")
    print(f"{'Framework':<15} | {'Size (KB)':<10}")
    print("-" * 30)
    for fw, data in sorted_metrics.items():
        kb = data['bundle_size'] / 1024
        print(f"{fw.capitalize():<15} | {kb:<10.2f}")

    # Generate reports
    with open('benchmark_results.json', 'w') as f:
        json.dump(sorted_metrics, f, indent=4)
        
    generate_svg_report(sorted_metrics)
    print("\nReports generated: benchmark_results.json, benchmark_results.svg")

if __name__ == "__main__":
    main()