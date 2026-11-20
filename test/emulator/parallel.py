#!/usr/bin/env python3
"""Run emulator suites concurrently in isolated Linux namespaces."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

import run


def active_sdk():
    home = Path.home()
    legacy = home / ".pebble-sdk"
    data = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    current = (legacy if legacy.exists() else data / "pebble-sdk") / "SDKs/current"
    sdk = current.resolve(strict=True)
    manifest = json.loads((sdk / "sdk-core/manifest.json").read_text())
    return sdk, manifest["version"]


def copy_workspace(source, destination, excluded, exclude_build=False):
    def ignore(directory, names):
        ignored = {name for name in names if name in {".git", "__pycache__"}}
        ignored.update(name for name in names if Path(directory, name).resolve() == excluded)
        if exclude_build and Path(directory) == source:
            ignored.add("build")
            ignored.update(name for name in names if name.startswith(".lock-waf"))
        return ignored

    shutil.copytree(source, destination, symlinks=True, ignore=ignore)


def prepare_home(home, version):
    sdk_dir = home / ".local/share/pebble-sdk/SDKs"
    sdk_dir.mkdir(parents=True)
    (sdk_dir / version).symlink_to("/tmp/sdk", target_is_directory=True)
    (sdk_dir / "current").symlink_to("/tmp/sdk", target_is_directory=True)
    # The isolated network is offline, so suppress the CLI's periodic update checks.
    settings = {"update-notify": {
        component: {"timestamp": time.time() + 7 * 86400, "version": "0"}
        for component in ("pebble-tool", "sdk-core")
    }}
    (sdk_dir.parent / "settings.json").write_text(json.dumps(settings))


def worker_command(workspace, home, result, sdk, platform, mode, args):
    runner = ["python3", "test/emulator/run.py", "--tier", args.tier,
              "--mode", mode, "--platform", platform,
              "--pbw", f"/tmp/workspace/build/parallel-packages/{mode}.pbw",
              "--output", "/tmp/results"]
    for pattern in args.case:
        runner += ["--case", pattern]
    for group in args.group:
        runner += ["--group", group]
    return [
        "unshare", "--user", "--map-root-user", "--net", "sh", "-c",
        'ip link set lo up && exec "$@"', "sh",
        "bwrap", "--ro-bind", "/", "/", "--proc", "/proc", "--dev", "/dev",
        "--tmpfs", "/tmp", "--tmpfs", "/var/tmp",
        "--ro-bind", str(sdk), "/tmp/sdk",
        "--bind", str(workspace), "/tmp/workspace",
        "--bind", str(home), "/tmp/worker-home", "--bind", str(result), "/tmp/results",
        "--unshare-pid", "--die-with-parent",
        "--setenv", "HOME", "/tmp/worker-home",
        "--setenv", "XDG_DATA_HOME", "/tmp/worker-home/.local/share",
        "--setenv", "XDG_CONFIG_HOME", "/tmp/worker-home/.config",
        "--setenv", "XDG_CACHE_HOME", "/tmp/worker-home/.cache",
        "--setenv", "XDG_RUNTIME_DIR", "/tmp",
        "--unsetenv", "TMPDIR", "--unsetenv", "PYTHONPATH",
        "--chdir", "/tmp/workspace", "--",
        "sh", "-c",
        'pebble emu-set-location --emulator "$1" --vnc 40 -105 && '
        'reply=$(printf "%s\\n" '
        '\'pebble.send_and_read(protocol.AppRunState(data=protocol.AppRunStateRequest()), '
        'protocol.AppRunState, timeout=15); print("TEST_WATCH_READY")\' '
        '| pebble repl --emulator "$1" --vnc) && '
        'printf "%s\\n" "$reply" && '
        'printf "%s\\n" "$reply" | grep -qx "TEST_WATCH_READY" && '
        'shift && exec "$@"',
        "sh", platform, *runner,
    ]


def run_worker(base, scratch, output, sdk, version, mode, platform, args):
    name = f"{mode}-{platform}"
    workspace = scratch / name / "workspace"
    home = scratch / name / "home"
    result = output / "workers" / mode / platform
    result.mkdir(parents=True)
    try:
        copy_workspace(base, workspace, output)
        prepare_home(home, version)
        command = worker_command(workspace, home, result, sdk, platform, mode, args)
        with (result / "worker.log").open("w") as log:
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        summary_path = result / "summary.json"
        if completed.returncode:
            return {"mode": mode, "platform": platform, "success": False,
                    "error": f"Worker exited {completed.returncode}; see worker.log"}
        if not summary_path.is_file():
            return {"mode": mode, "platform": platform, "success": False,
                    "error": "Worker exited successfully without a summary"}
        summary = json.loads(summary_path.read_text())
        return {"mode": mode, "platform": platform,
                "success": summary.get("success") is True,
                "summary": str(summary_path.relative_to(output))}
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        return {"mode": mode, "platform": platform, "success": False, "error": str(error)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tier", choices=("sanity", "comprehensive"), default="sanity")
    parser.add_argument("--mode", choices=("release", "debug", "both"))
    parser.add_argument("--platform", action="append")
    parser.add_argument("--pbw", type=Path, help="Reuse a PBW (requires one mode)")
    parser.add_argument("--case", action="append", default=[], metavar="GLOB")
    parser.add_argument("--group", action="append", choices=run.GROUPS, default=[])
    parser.add_argument("--jobs", type=int, default=2, help="Maximum concurrent workers (default: 2)")
    parser.add_argument("--output", type=Path, default=run.ROOT / "build/emulator-parallel")
    args = parser.parse_args()
    args.mode = args.mode or ("both" if args.tier == "sanity" else "release")
    supported = json.loads((run.ROOT / "package.json").read_text())["pebble"]["targetPlatforms"]
    platforms = args.platform or supported
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if any(platform not in supported for platform in platforms):
        parser.error("Platform must be a supported targetPlatforms entry")
    if len(set(platforms)) != len(platforms):
        parser.error("Each platform may be selected only once")
    if args.pbw and args.mode == "both":
        parser.error("--pbw requires --mode release or --mode debug")
    for executable in ("unshare", "bwrap", "ip", "pebble"):
        if not shutil.which(executable):
            parser.error(f"{executable} is required")
    try:
        sdk, version = active_sdk()
    except (OSError, ValueError, KeyError) as error:
        parser.error(f"Cannot find the active Pebble SDK: {error}")
    output_parent = args.output.resolve()
    output_parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=output_parent))
    print(f"Reports: {output}", flush=True)
    summary = {"tier": args.tier, "mode": args.mode, "platforms": platforms,
               "jobs": args.jobs, "case_patterns": args.case, "groups": args.group,
               "success": False, "started": time.time(), "workers": []}
    try:
        with tempfile.TemporaryDirectory(prefix="pebble-parallel-") as directory:
            scratch = Path(directory)
            base = scratch / "base"
            copy_workspace(run.ROOT, base, output, exclude_build=True)
            packages = base / "build/parallel-packages"
            packages.mkdir(parents=True)
            selector = [sys.executable, "test/emulator/run.py", "--tier", args.tier, "--list-cases"]
            for pattern in args.case:
                selector += ["--case", pattern]
            for group in args.group:
                selector += ["--group", group]
            subprocess.run(selector, cwd=base, check=True)
            modes = ("release", "debug") if args.mode == "both" else (args.mode,)
            for mode in modes:
                if args.pbw:
                    shutil.copyfile(args.pbw.resolve(strict=True), packages / f"{mode}.pbw")
                else:
                    subprocess.run(["pebble", "build", "--", f"--mode={mode}"], cwd=base, check=True)
                    built = list((base / "build").glob("*.pbw"))
                    if len(built) != 1:
                        raise RuntimeError("Expected exactly one built PBW")
                    shutil.copyfile(built[0], packages / f"{mode}.pbw")
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = [executor.submit(run_worker, base, scratch, output, sdk, version,
                                           mode, platform, args)
                           for mode in modes for platform in platforms]
                for future in as_completed(futures):
                    result = future.result()
                    summary["workers"].append(result)
                    status = "PASS" if result["success"] else "FAIL"
                    print(f'{status} {result["mode"]}/{result["platform"]}', flush=True)
            summary["success"] = all(worker["success"] for worker in summary["workers"])
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        summary["error"] = str(error) or type(error).__name__
        print(f"Parallel suite failed: {summary['error']}", file=sys.stderr)
    finally:
        summary["finished"] = time.time()
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
