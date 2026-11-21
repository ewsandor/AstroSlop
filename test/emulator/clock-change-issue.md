# Legacy-platform clock changes stop the app responding

Issue draft discovered while implementing #46. File this as a separate bug and
replace the draft path in `CLOCK_CHANGE_QUARANTINE` with the resulting issue URL.

## Failure

SDK 4.44's Basalt emulator hard-faults in the application task after changing UTC
while the release application is running. The app stops answering AppMessage
captures, firmware writes a core dump, and the watch reboots. Small backward
changes reproduce the failure; this is not limited to large reference-date jumps.

Chalk and Diorite release checks also stop responding during the Sun-to-Moon
reference transition. Debug Basalt survived small and large changes in initial
triage. The expanded regression also lost the debug/Basalt response on the
180-day forward Moon transition; debug is not universally unaffected.
The final debug/Chalk sweep passed the dedicated backward/forward case but
subsequently stopped answering when Mars fixture time changed from January 1
midnight to noon. Its last successful capture was Mars at UTC 1767225609;
the request after setting UTC 1767268800 received no renderer response.
This is a missing-response failure, not a confirmed native hard fault.

An initial unchanged-HEAD Basalt install also stopped answering after a UTC
change, but that emulator had already experienced the fault. Fresh unchanged-HEAD
Chalk/Diorite probes did not produce a native hard fault, and Diorite remained
responsive. Do not yet classify the bug as pre-existing or rule out instrumentation:
repeat matched, fresh-emulator comparisons and examine release LTO, logging, and
memory effects. Baseline release footprint was about 54 KB versus about 58 KB
with capture/error logging; both fit 64 KB, but peak stack/heap must be checked.

## Reproduction

Use SDK 4.44, headless `--vnc`, and a release build:

```sh
pebble build
pebble install --emulator basalt --vnc --logs --qemu_logs
# In another terminal, while the application is running:
pebble emu-set-time --emulator basalt --vnc --utc 1767268800
pebble screenshot --emulator basalt --vnc --scale 6 --no-open failure.png
```

Stop the log stream once diagnostics are captured. The screenshot command may
fail because the application/watch has stopped responding. Repeat with
`pebble build -- --mode=debug` to compare behavior.

The automated `live-clock-change` case supplies a fresh manual location, repeats
the first Sun/Moon reference transitions (January 1 noon to midnight), then makes
two-minute backward and 180-day forward changes. It requires the app to respond
and report the expected UTC time after each change. It is
currently quarantined **for release/Basalt, Chalk and Diorite, and debug/Basalt
and Chalk**,
in both suites. Astronomy,
navigation, formatting, and screenshot checks remain enabled; their fixture UTC
is set with the app stopped, followed by a fresh launch. This is explicit fixture
setup, not a retry or recovery from failed checks. Reports record the quarantine.
The SDK app-run-state protocol acknowledges stopping and renderer captures
confirm startup; fixture changes do not reinstall the application.

For an exact automated reproduction, temporarily remove the affected platform's
entry from `CLOCK_CHANGE_QUARANTINE` locally and run:

```sh
python3 test/emulator/run.py --tier sanity --mode release --platform basalt
```

Restore the quarantine unless the root-cause fix is validated. Do not remove or
weaken the regression to make the gate green.

## Captured diagnostic

```text
Task: App <PushTo Too
HFSR=0x40000000 (forced)
CFSR=0x82 (MMFARVALID, DACCVIOL)
MMFAR=0x20018018
R0=0x20018040  R1=0  R2=0x10003948  R3=0x20000dec
R12=0x20018080  SP=0x2000cf40  LR=0xfffffffd
PC=0x08060014  PSR=0x6100000e
```

## Proposed fix

The root cause is not yet established. Investigate the minute-tick/time-change
path and the invalid memory access using **matching firmware and app symbols**.
Compare release LTO code generation and callback/PIC relocation with debug,
including the capture/error-logging memory and API effects.
Capture-only inbox requests currently also run the normal recalculation/timer
rescheduling path. Check whether separating observation from settings updates
avoids perturbing that path; this is a candidate to investigate, not an
established fix.
Resolve the actual invalid access or SDK compatibility issue with a minimal fix;
do not guess from an unmatched firmware ELF's symbol names.

Do not hide this failure with reinstall-on-error, larger timeouts, or relaxed
astronomy tolerances. If a compiler/build change is necessary, validate its RAM
cost on every 64 KB platform before adopting it.

## Required acceptance criteria — do not close without all of these

- [ ] Establish and fix the root cause; verify release and debug on all affected platforms.
- [ ] Remove all affected release and debug entries from `CLOCK_CHANGE_QUARANTINE`, so
      `live-clock-change` is **re-enabled**, not skipped or deleted.
- [ ] Pass local release/debug sanity and comprehensive checks on affected platforms with
      the re-enabled regression, including backward and large UTC changes.
- [ ] Pass sanity on every supported variant and the release comprehensive
      matrix; retain the final reports.
- [ ] Check 64 KB RAM budgets and confirm other application behavior is preserved.

Re-enabling and passing the regression is a requirement for closing this bug.
