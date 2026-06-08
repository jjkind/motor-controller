#!/usr/bin/env python3
"""
Plot three semicolon-delimited current columns versus time.

Expected file format:
    IA;IB;IC

Example:
    -13;61;-950
    6;68;-2520

Each valid row is treated as one sample.
Default sample period:
    1 ms per row = 0.001 seconds/sample

PlatformIO monitor lines starting with "---" are ignored.

Usage from project root:
    python test/plot_current_columns.py logs/current_log.csv

Optional:
    python test/plot_current_columns.py logs/current_log.csv --output logs/current_plot.png
"""

from pathlib import Path
import argparse
import matplotlib.pyplot as plt


SAMPLE_PERIOD_SEC = 0.02  # Coordinate with CURRENT_PRINT_DECIMATION in src/main.cpp


def read_current_log(file_path: str | Path):
    file_path = Path(file_path)

    ia_values = []
    ib_values = []
    ic_values = []

    with file_path.open("r", encoding="utf-8", errors="replace") as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()

            # Skip empty lines and PlatformIO monitor header/status lines.
            if not line or line.startswith("---"):
                continue

            parts = line.split(";")

            if len(parts) != 3:
                print(f"Skipping malformed line {line_number}: {line}")
                continue

            try:
                ia = float(parts[0])
                ib = float(parts[1])
                ic = float(parts[2])
            except ValueError:
                print(f"Skipping non-numeric line {line_number}: {line}")
                continue

            ia_values.append(ia)
            ib_values.append(ib)
            ic_values.append(ic)

    if not ia_values:
        raise ValueError("No valid IA/IB/IC samples found.")

    time_s = [index * SAMPLE_PERIOD_SEC for index in range(len(ia_values))]

    return time_s, ia_values, ib_values, ic_values


def plot_currents(time_s, ia_values, ib_values, ic_values, output_path: str | Path | None = None):
    plt.figure(figsize=(12, 6))

    plt.plot(time_s, ia_values, label="IA", color="red")
    plt.plot(time_s, ib_values, label="IB", color="blue")
    plt.plot(time_s, ic_values, label="IC", color="gold")

    plt.xlabel("Time (s)")
    plt.ylabel("Current (mA)")
    plt.title("Current values versus time")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    if output_path:
        output_path = Path(output_path)
        plt.savefig(output_path, dpi=150)
        print(f"Saved plot to: {output_path}")

    plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Plot three semicolon-delimited current columns versus time."
    )
    parser.add_argument("csv_file", help="Path to current_log.csv")
    parser.add_argument("--output", "-o", help="Optional output image path, e.g. logs/current_plot.png")
    args = parser.parse_args()

    time_s, ia_values, ib_values, ic_values = read_current_log(args.csv_file)

    print(f"Loaded {len(time_s)} samples")
    print(f"Sample period: {SAMPLE_PERIOD_SEC} s")
    print(f"Total duration: {time_s[-1]:.3f} s")

    plot_currents(time_s, ia_values, ib_values, ic_values, args.output)


if __name__ == "__main__":
    main()
