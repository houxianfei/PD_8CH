"""Independently parse exported CSV fixtures with Python's standard CSV parser."""
import csv
import datetime as dt
import math
import pathlib
import sys


def verify(path, expected_channels, offset_ms=0):
    with path.open(encoding="utf-8-sig", newline="") as source:
        parsed = list(csv.reader(source))
    metadata = {}
    channels = {}
    data = []
    for row in parsed:
        if row[0].startswith("# "):
            if row[0] == "# channel":
                channels[int(row[1][2:])] = (row[2], float(row[4]), float(row[6]))
            else:
                metadata[row[0][2:]] = row[1]
        else:
            data.append(row)
    assert sorted(channels) == expected_channels
    assert int(metadata["selected_channel_count"]) == len(expected_channels)
    assert metadata["timestamp_timezone"] == "UTC"
    assert metadata["calibration_formula"] == "Power = P1 * (P0 + ADC)"
    assert metadata["calibration_file"] == "cfg/custom, calibration.cfg"
    assert metadata["acquisition_start_utc"] == "2026-11-19T00:00:00.123456Z"
    assert metadata["data_end_utc"] == ("2026-11-19T00:00:01.133456Z" if offset_ms else "2026-11-19T00:00:00.133456Z")
    assert int(metadata["exported_sample_count"]) == 3
    header, *rows = data
    assert header[0] == "timestamp_utc"
    assert len(header) == 1 + 2 * len(expected_channels)
    assert len(rows) == 3
    base = dt.datetime(2026, 11, 19, 0, 0, 0, 123456, tzinfo=dt.timezone.utc)
    for index, row in enumerate(rows):
        assert len(row) == len(header)
        actual = dt.datetime.fromisoformat(row[0].replace("Z", "+00:00"))
        assert actual == base + dt.timedelta(milliseconds=offset_ms + [0, 1, 10][index])
        for position, channel in enumerate(expected_channels):
            name, p0, p1 = channels[channel]
            assert header[1 + position * 2] == f"CH{channel} [{name}] ADC"
            assert header[2 + position * 2] == f"CH{channel} [{name}] Power"
            adc = int(row[1 + position * 2])
            power = float(row[2 + position * 2])
            assert power == p1 * (p0 + adc)
    if path.stem == "running_snapshot":
        assert metadata["acquisition_end_utc"] == ""
        assert metadata["acquisition_state"] == "Collecting"
    elif offset_ms:
        assert metadata["acquisition_end_utc"] == "2026-11-19T00:00:01.143456Z"
        assert int(metadata["overwritten_sample_count"]) == 100
        assert metadata["data_start_utc"] == "2026-11-19T00:00:01.123456Z"
    else:
        assert metadata["acquisition_end_utc"] == "2026-11-19T00:00:00.143456Z"


def verify_simulation(path, expected_count, rate):
    with path.open(encoding="utf-8-sig", newline="") as source:
        parsed = list(csv.reader(source))
    meta = {r[0][2:]: r[1] for r in parsed if r[0].startswith("# ") and r[0] != "# channel"}
    table = [r for r in parsed if not r[0].startswith("# ")]
    header, *rows = table
    assert meta["data_source"] == "simulation"
    assert meta["timestamp_source"] == "host_start_command_plus_simulated_relative_timestamp"
    assert meta["device_serial"] == "SIM-8CH"
    assert int(meta["exported_sample_count"]) == expected_count
    assert float(meta["sample_rate_hz"]) == rate
    assert len(header) == 5 and len(rows) == expected_count
    base = dt.datetime.fromisoformat(meta["acquisition_start_utc"].replace("Z", "+00:00"))
    for index, row in enumerate(rows):
        assert dt.datetime.fromisoformat(row[0].replace("Z", "+00:00")) == base + dt.timedelta(seconds=index / rate)
        for column, channel in [(1, 1), (3, 3)]:
            value = round(250 * channel + (1200 + 800 * channel) * math.sin(
                2 * math.pi * (0.5 + 0.125 * channel) * index / rate + 0.35 * channel))
            assert int(row[column]) == value
            assert float(row[column + 1]) == (3 * (2 + value) if channel == 1 else value)


root = pathlib.Path(sys.argv[1])
verify(root / "selected_one.csv", [1])
verify(root / "selected_two.csv", [1, 3])
verify(root / "all_channels.csv", list(range(8)))
verify(root / "running_snapshot.csv", list(range(8)))
verify(root / "retained_window.csv", list(range(8)), 1000)
verify_simulation(root / "simulation.csv", 2, 1)
verify_simulation(root.parent / ("sdk_demo_" + root.name[4:] + ".csv"), 100, 100)
print("PASS Python CSV verification: names, selection, exact timestamps, metadata and calibrated values")
