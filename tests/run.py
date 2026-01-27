#!/usr/bin/env python3
"""
Coi Test Runner
"""

import argparse
from pathlib import Path

# Fix module search path if needed
import sys
sys.path.append(str(Path(__file__).parent))

from runner.unit import UnitRunner
from runner.integration import IntegrationRunner
from runner.gallery import GalleryRunner

# Paths
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent

def main():
    parser = argparse.ArgumentParser(description="Coi Test Runner")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Unit Tests
    p_unit = subparsers.add_parser("unit", help="Run unit tests")

    # Gallery
    p_gallery = subparsers.add_parser("gallery", help="Run web visual gallery")
    p_gallery.add_argument("--scene", help="Scene name filter (e.g. input_*)")
    p_gallery.add_argument("--out", help="Output dir", default="tests/integration/web/.cache/visual")
    p_gallery.add_argument("--size", help="Screenshot size", default="960x540")
    p_gallery.add_argument("--browser", help="Browser binary path")

    p_gallery.add_argument("--open", action="store_true", help="Open gallery after run")

    # Integration
    p_it = subparsers.add_parser("integration", help="Run web integration tests")
    p_it.add_argument("--scene", help="Scene name filter")
    p_it.add_argument("--out", help="Output dir", default="tests/integration/web/.cache/integration")
    p_it.add_argument("--size", help="Viewport size", default="960x540")
    p_it.add_argument("--browser", help="Browser binary path")

    p_it.add_argument("--headed", action="store_true", help="Run headed")

    # List
    p_list = subparsers.add_parser("list", help="List available scenes")
    p_list.add_argument("--scene", help="Filter scenes")

    # All
    p_all = subparsers.add_parser("all", help="Run all tests (unit + integration)")
    p_all.add_argument("--browser", help="Browser binary path")
    p_all.add_argument("--out", help="Output dir", default="tests/integration/web/.cache/integration")
    p_all.add_argument("--size", help="Viewport size", default="960x540")
    p_all.add_argument("--headed", action="store_true", help="Run headed")
    # Note: 'scene' filter applies to integration tests only in 'all' mode
    p_all.add_argument("--scene", help="Scene name filter")

    args = parser.parse_args()

    if args.command == "unit":
        runner = UnitRunner(PROJECT_ROOT)
        runner.run(SCRIPT_DIR)
        
    elif args.command == "integration":
        runner = IntegrationRunner(PROJECT_ROOT)
        runner.run(args)

    elif args.command == "all":
        print("==> Running UNIT tests")
        unit = UnitRunner(PROJECT_ROOT)
        unit.run(SCRIPT_DIR)
        print("\n==> Running INTEGRATION tests")
        integration = IntegrationRunner(PROJECT_ROOT)
        integration.run(args)
        
    elif args.command == "gallery":
        runner = GalleryRunner(PROJECT_ROOT)
        runner.run(args)

    elif args.command == "list":
        # Reuse integration runner to parse manifest
        runner = IntegrationRunner(PROJECT_ROOT)
        scenes = runner.parse_manifest(args.scene)
        for name, _ in scenes:
            print(name)

if __name__ == "__main__":
    main()

