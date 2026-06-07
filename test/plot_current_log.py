#!/usr/bin/env python3
"""
Plot three current values from a PlatformIO serial log.

Expected data lines look like:
    IA;IB;IC

Example:
    -13;61;-950
    6;68;-2520

PlatformIO monitor header lines beginning with "---" are ignored.

For this log:
    SAMPLE_PERIOD_SEC = 0.001  # 1 ms per sample
"""

from pathlib import Path
import argparse
import pandas as pd
import matplotlib.pyplot as plt


# Change this value if your sample period changes.
SAMPLE_PERIOD_SEC = 0.001  # seconds/sample, 1 ms at 1 kHz sampling


def read_current_log(csv_path: str | Path) -> pd.DataFrame:
    rows = []

    with open(csv_path, "r", encoding="utf-8", errors="replace") as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()

            # Skip empty lines and PlatformIO monitor messages.
            if not line or line.startswith("---"):
                continue

            # Your current log is semicolon-delimited: IA;IB;IC
            parts = line.split(";")

            if len(parts) != 3:
                print(f"Skipping malformed line {line_number}: {line}")
                continue

            try:
                ia_ma = float(parts[0])
                ib_ma = float(parts[1])
                ic_ma = float(parts[2])
            except ValueError:
                print(f"Skipping non-numeric line {line_number}: {line}")
                continue

            rows.append((ia_ma, ib_ma, ic_ma))

    if not rows:
        raise ValueError("No valid current samples found in the file.")

    df = pd.DataFrame(rows, columns=["IA_mA", "IB_mA", "IC_mA"])
    df["time_s"] = df.index * SAMPLE_PERIOD_SEC
    return df


def plot_currents(df: pd.DataFrame, output_path: str | Path | None = None) -> None:
    plt.figure(figsize=(12, 6))

    plt.plot(df["time_s"], df["IA_mA"], label="IA")
    plt.plot(df["time_s"], df["IB_mA"], label="IB")
    plt.plot(df["time_s"], df["IC_mA"], label="IC")

    plt.xlabel("Time (s)")
    plt.ylabel("Current (mA)")
    plt.title("Motor phase currents versus time")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    if output_path:
        plt.savefig(output_path, dpi=150)
        print(f"Saved plot to: {output_path}")

    plt.show()


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot IA, IB, and IC current values versus time.")
    parser.add_argument("csv_file", help="Path to current log CSV file")
    parser.add_argument("--output", "-o", help="Optional output image path, e.g. current_plot.png")
    args = parser.parse_args()

    df = read_current_log(args.csv_file)

    print(f"Loaded {len(df)} samples")
    print(f"Sample period: {SAMPLE_PERIOD_SEC} s")
    print(f"Total duration: {df['time_s'].iloc[-1]:.3f} s")

    plot_currents(df, args.output)


if __name__ == "__main__":
    main()
