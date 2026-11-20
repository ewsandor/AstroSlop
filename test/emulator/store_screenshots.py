#!/usr/bin/env python3
"""Capture native-resolution store listing candidates from a release PBW."""

import argparse
import json
from pathlib import Path
import subprocess
import time
import zipfile

from PIL import Image, ImageOps

import run

TIMESTAMP = 1767225600
LATITUDE, LONGITUDE, HEIGHT = 51.4779, 0.0, 45
SCENES = (
    ("main", 0, "Jupiter", False, False),
    ("banner", 1, "Sirius", True, False),
    ("inverted-main", 2, "M31", False, True),
    ("inverted-banner", 2, "M42", True, True),
    ("red-backlight", 1, "Betelgeuse", False, True),
)


def object_index(objects, catalog, name):
    matches = [i for i, obj in enumerate(objects[catalog])
               if name in (obj["id"], obj["name"])]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one catalog entry for {name}")
    return matches[0]


def validate_capture(screen, target, details, banner_message, catalog, index, label,
                     banner, inverted):
    actual_catalog, actual_index, observed = target
    if (screen[0], screen[1], screen[3], actual_catalog, actual_index) != (
            str(int(banner)), str(int(inverted)), label, catalog, index):
        raise AssertionError(f"Unexpected capture: {screen}, {actual_catalog}, {actual_index}")
    if not TIMESTAMP <= observed < TIMESTAMP + 60 or details[3] != "12:00 AM":
        raise AssertionError(f"Capture clock is outside the fixture minute: {observed}, {details[3]}")
    if details[2] != "0" or details[7] or (banner and banner_message):
        raise AssertionError(f"Capture contains a warning: {details}, {banner_message}")
    if not banner and details[4] != "51\N{DEGREE SIGN}F":
        raise AssertionError(f"Capture is missing the weather fixture: {details}")
    if not banner and not details[5]:
        raise AssertionError("Capture is missing the rise/set event")
    run.parse_angles((screen[4], screen[5]), False)


def wait_for_watch(emulator):
    script = ("pebble.send_and_read(protocol.AppRunState(data=protocol.AppRunStateRequest()), "
              "protocol.AppRunState, timeout=15); print('STORE_WATCH_READY')\n")
    args = emulator.args("repl")
    emulator.commands.write(" ".join(args) + " # watch readiness\n")
    emulator.commands.flush()
    result = subprocess.run(args, input=script, cwd=run.ROOT, env=emulator.env,
                            text=True, capture_output=True, timeout=60)
    emulator.commands.write(result.stdout + result.stderr)
    emulator.commands.flush()
    if result.returncode or "STORE_WATCH_READY" not in result.stdout.splitlines():
        raise RuntimeError(f"Watch did not become ready; see {emulator.output / 'commands.log'}")


def wait_for_scene(emulator, settings, require_event):
    location = {key: value for key, value in settings.items() if key.startswith("LOC_")}
    deadline = time.monotonic() + 30
    while True:
        screen, _, _ = emulator.capture()
        if emulator.details[2] != "0":
            emulator.capture(location)
        elif screen[4] != "---.-\N{DEGREE SIGN}" and (not require_event or emulator.details[5]):
            return
        if time.monotonic() >= deadline:
            raise TimeoutError("Screenshot scene did not finish calculating without a location warning")
        time.sleep(0.1)


def grab(emulator, path, banner, fixture):
    emulator.sequence += 1
    sequence = emulator.sequence
    if banner:
        emulator.sequence += 1
    verification = emulator.sequence
    values = {emulator.keys[key]: value for key, value in
              {**fixture, "TEST_CAPTURE": sequence}.items()}
    script = f"""
import time
from pathlib import Path
from uuid import UUID
import png
from libpebble2.services.appmessage import AppMessageService, Int32
from libpebble2.services.screenshot import Screenshot
from pebble_tool.commands.emucontrol import send_data_to_qemu
from pebble_tool.commands.screenshot import ScreenshotCommand
from libpebble2.communication.transports.qemu.protocol import QemuButton
service = AppMessageService(pebble)
try:
    if {banner!r}:
        send_data_to_qemu(pebble.transport, QemuButton(state=QemuButton.Button.Down))
        time.sleep(0.1)
        send_data_to_qemu(pebble.transport, QemuButton(state=0))
        time.sleep(0.2)
    service.send_message(UUID({emulator.uuid!r}), {{key: Int32(value) for key, value in {values!r}.items()}})
    deadline = time.monotonic() + 10
    while f"TEST_DETAIL {sequence} " not in Path({str(emulator.log_path)!r}).read_text(errors="replace"):
        if time.monotonic() >= deadline:
            raise TimeoutError("Screenshot state did not render")
        time.sleep(0.02)
    image = Screenshot(pebble).grab_image()
    if {banner!r}:
        service.send_message(UUID({emulator.uuid!r}), {{{emulator.keys["TEST_CAPTURE"]!r}: Int32({verification})}})
        deadline = time.monotonic() + 10
        marker = f"TEST_SCREEN {verification} "
        while marker not in Path({str(emulator.log_path)!r}).read_text(errors="replace"):
            if time.monotonic() >= deadline:
                raise TimeoutError("Could not verify the banner after the screenshot")
            time.sleep(0.02)
        if marker + "1 " not in Path({str(emulator.log_path)!r}).read_text(errors="replace"):
            raise AssertionError("Banner expired during the screenshot")
    processor = ScreenshotCommand()
    processor.pebble = pebble
    png.from_array(processor._roundify(processor._correct_colours(image)), mode="RGBA;8").save({str(path)!r})
finally:
    send_data_to_qemu(pebble.transport, QemuButton(state=0))
    service.shutdown()
print("STORE_SCREENSHOT_COMPLETE")
"""
    args = emulator.args("repl")
    emulator.commands.write(" ".join(args) + f" # store screenshot {path.name}\n")
    emulator.commands.flush()
    result = subprocess.run(args, input=f"exec({script!r})\n", cwd=run.ROOT,
                            env=emulator.env, text=True, capture_output=True, timeout=60)
    emulator.commands.write(result.stdout + result.stderr)
    emulator.commands.flush()
    if result.returncode or "STORE_SCREENSHOT_COMPLETE" not in result.stdout:
        raise RuntimeError(f"Screenshot failed; see {emulator.output / 'commands.log'}")
    return emulator.read_capture(sequence)


def generate(platform, pbw, output):
    project = json.loads((run.ROOT / "package.json").read_text())
    with zipfile.ZipFile(pbw) as package:
        appinfo = json.loads(package.read("appinfo.json"))
    if appinfo["uuid"] != project["pebble"]["uuid"]:
        raise ValueError("Store screenshots require the release PBW")
    objects = run.catalogs()
    work = output / "logs" / platform
    work.mkdir(parents=True, exist_ok=True)
    images = output / platform
    images.mkdir(parents=True, exist_ok=True)
    emulator = run.Emulator(platform, pbw, work, appinfo)
    emulator.env["TZ"] = "UTC"
    try:
        wait_for_watch(emulator)
        emulator.start()
        emulator.run("emu-time-format", "--format", "12h")
        for name, catalog, obj_name, banner, inverted in SCENES:
            if name == "red-backlight" and platform != "emery":
                continue
            print(f"{platform}: {name} ({obj_name})", flush=True)
            index = object_index(objects, catalog, obj_name)
            case = run.Case(name, catalog, index, 2440587.5 + TIMESTAMP / 86400,
                            LATITUDE, LONGITUDE, HEIGHT, True, (), inverted=inverted)
            settings = {**run.fixture_message(case, TIMESTAMP),
                        "CFG_SHOW_TIME": 1, "CFG_SHOW_EVENT": 1,
                        "CFG_SHOW_MAG": 1, "CFG_LABEL_MODE": 2,
                        "CFG_USE_WEATHER": 1, "WX_TEMP_DC": 106,
                        "WX_TEMP_UNIT": 1, "WX_EXPIRES": TIMESTAMP + 3600}
            emulator.capture(settings)
            emulator.navigate(catalog, index, objects)
            # Deinitialization saves the selection; changing the clock with the
            # app stopped avoids the known live-clock regression.
            emulator.set_reference_time(TIMESTAMP, live_clock=False)
            emulator.reference_timestamp = None
            emulator.capture({**settings, "WX_CLEAR": 0, "CFG_BANNER": 0,
                              "CFG_TINT": int(name == "red-backlight")})
            if banner:
                # Warm the selected object's position before opening its brief
                # banner, especially on slower solar-system calculations.
                emulator.navigate(catalog, (index - 1) % len(objects[catalog]), objects)
                emulator.capture({"CFG_BANNER": 1})
            wait_for_scene(emulator, settings, require_event=not banner)
            path = images / f"{platform}-{name}.png"
            path.unlink(missing_ok=True)
            fixture = {key: value for key, value in settings.items()
                       if key.startswith("WX_")}
            screen, target, _ = grab(emulator, path, banner, fixture)
            expected_label = objects[catalog][index]["id"]
            if expected_label and objects[catalog][index]["name"]:
                expected_label += "  "
            expected_label += objects[catalog][index]["name"]
            validate_capture(screen, target, emulator.details, emulator.banner_message,
                             catalog, index, expected_label, banner, inverted)
            with Image.open(path) as image:
                if image.size != run.SIZES[platform]:
                    raise AssertionError(f"Unexpected image size: {image.size}")
                if name == "red-backlight":
                    # The screenshot protocol returns framebuffer pixels, not the
                    # physical backlight. Simulate red illumination explicitly.
                    red = ImageOps.colorize(image.convert("L"), black="black", white="red")
                    red.save(path)
            if run.fatal_error(emulator.logs()):
                raise AssertionError("App error detected after screenshot")
    finally:
        emulator.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=run.SIZES)
    parser.add_argument("--pbw", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.platform, args.pbw.resolve(strict=True), args.output.resolve())


if __name__ == "__main__":
    main()
