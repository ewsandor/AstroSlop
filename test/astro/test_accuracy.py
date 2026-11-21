import math
import subprocess
import sys


def read_output(command):
    output = subprocess.check_output(command, text=True)
    positions = {}
    events = {}
    mean_obliquities = {}
    mode = None
    for line in output.splitlines():
        fields = line.split()
        if fields[0] == "C":
            mode = fields[1]
        elif fields[0] == "P":
            positions[tuple(fields[1:5])] = tuple(map(float, fields[5:7]))
        elif fields[0] == "E":
            events[fields[1]] = (int(fields[2]), float(fields[3]))
        elif fields[0] == "M":
            mean_obliquities[fields[1]] = tuple(map(float, fields[2:5]))
    return mode, positions, events, mean_obliquities


def separation_deg(first, second):
    ra1, dec1 = map(math.radians, first)
    ra2, dec2 = map(math.radians, second)
    haversine = math.sin((dec2 - dec1) / 2) ** 2 + (
        math.cos(dec1) * math.cos(dec2) * math.sin((ra2 - ra1) / 2) ** 2
    )
    return math.degrees(2 * math.asin(math.sqrt(min(1, haversine))))


def main():
    full = read_output([sys.argv[1], sys.argv[3]])
    low = read_output([sys.argv[2], sys.argv[3]])
    assert full[0] == "full" and low[0] == "low"
    assert full[1].keys() == low[1].keys()
    assert full[3].keys() == low[3].keys()
    for date, expected in full[3].items():
        actual = low[3][date]
        assert actual[0] == 0 and actual[1] == 0
        assert math.isclose(actual[2], expected[2], rel_tol=0, abs_tol=1e-12)

    maxima = {}
    worst = None
    for key, expected in full[1].items():
        error = separation_deg(expected, low[1][key])
        category = key[0]
        maxima[category] = max(maxima.get(category, 0), error)
        if worst is None or error > worst[0]:
            worst = (error, key)
    for category, error in sorted(maxima.items()):
        print(f"{category} maximum correction-off position error: {error:.6f} degrees")
    print(f"Overall maximum: {worst[0]:.6f} degrees ({' '.join(worst[1])})")
    assert worst[0] <= 0.012
    assert worst[0] < 0.1  # Low-memory builds format coordinates to tenths of a degree.

    assert full[2].keys() == low[2].keys()
    max_event_seconds = 0
    for name, (full_kind, full_jd) in full[2].items():
        low_kind, low_jd = low[2][name]
        assert full_kind == low_kind, (name, full_kind, low_kind)
        if full_kind in (1, 2):  # ASTRO_EVENT_RISE / ASTRO_EVENT_SET
            seconds = abs(full_jd - low_jd) * 86400
            max_event_seconds = max(max_event_seconds, seconds)
            print(f"{name} rise/set change: {seconds:.2f} seconds")
            assert seconds <= 60, (name, seconds)
    print(f"Maximum rise/set change: {max_event_seconds:.2f} seconds")


if __name__ == "__main__":
    main()
