#!/usr/bin/env python3
"""
Plot three current values from a PlatformIO serial log.

Expected data lines look like:
    IA;IB;IC

Example:
    -13;61;-950
    6;68;-2520

PlatformIO monitor header lines beginning with "---" are ignored.

Original sample rate:
    SAMPLE_PERIOD_SEC = 0.001  # 1 ms per sample, 1000 Hz

Processing pipeline:
    1. Read raw IA/IB/IC current samples at 1000 Hz.
    2. Average every 5 samples.
       Effective sample rate becomes 200 Hz.
    3. Apply a 2nd-order Butterworth low-pass IIR filter.
       Cutoff frequency = 10 Hz.
       Filter sample rate = 200 Hz.

This script creates two plots:
    1. Unfiltered raw phase currents at 1000 Hz.
    2. Averaged + IIR-filtered phase currents at 200 Hz.
"""

from pathlib import Path
import argparse
import pandas as pd
import matplotlib.pyplot as plt


# Raw log sample period.
SAMPLE_PERIOD_SEC = 0.001  # seconds/sample, 1 ms at 1 kHz sampling

# Average every N raw samples before filtering.
AVERAGE_BLOCK_SIZE = 5

# Effective sample period/rate after averaging every 5 samples.
AVERAGED_SAMPLE_PERIOD_SEC = SAMPLE_PERIOD_SEC * AVERAGE_BLOCK_SIZE
AVERAGED_SAMPLE_RATE_HZ = 1.0 / AVERAGED_SAMPLE_PERIOD_SEC


# 2nd-order Butterworth low-pass IIR coefficients.
# Designed for:
#   sample rate = 200 Hz
#   cutoff      = 10 Hz
#
# Difference equation:
#   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
#        - a1*y[n-1] - a2*y[n-2]
#
# These coefficients correspond to scipy.signal.butter(2, 10, btype="low", fs=200).
IIR_B0 = 0.020083365564
IIR_B1 = 0.040166731128
IIR_B2 = 0.020083365564

IIR_A1 = -1.561018075801
IIR_A2 = 0.641351538057


def read_current_log(csv_path: str | Path) -> pd.DataFrame:
    rows = []

    with open(csv_path, "r", encoding="utf-8", errors="replace") as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()

            # Skip empty lines and PlatformIO monitor messages.
            if not line or line.startswith("---"):
                continue

            # Current log is semicolon-delimited: IA;IB;IC
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


def average_every_n_samples(df: pd.DataFrame, block_size: int) -> pd.DataFrame:
    """
    Average every block_size raw samples.

    If the final block has fewer than block_size samples, it is dropped so each
    averaged point represents the same number of raw samples.
    """
    usable_sample_count = (len(df) // block_size) * block_size

    if usable_sample_count == 0:
        raise ValueError(
            f"Not enough samples to average every {block_size} samples. "
            f"Loaded only {len(df)} samples."
        )

    trimmed = df.iloc[:usable_sample_count].copy()
    trimmed["block"] = trimmed.index // block_size

    averaged = (
        trimmed
        .groupby("block", as_index=False)
        .agg(
            IA_mA_avg=("IA_mA", "mean"),
            IB_mA_avg=("IB_mA", "mean"),
            IC_mA_avg=("IC_mA", "mean"),
        )
    )

    # Put the averaged sample time at the center of each 5-sample block.
    # For block 0, samples are at 0,1,2,3,4 ms, so center is 2 ms.
    averaged["time_s"] = (
        averaged["block"] * block_size + ((block_size - 1) / 2.0)
    ) * SAMPLE_PERIOD_SEC

    return averaged


def apply_iir2_lowpass_to_series(samples: pd.Series) -> list[float]:
    """
    Apply the 2nd-order Butterworth IIR low-pass filter to one current signal.

    The filter state is initialized with the first averaged sample. This reduces
    startup transient compared with starting the state at zero.
    """
    if samples.empty:
        return []

    first_sample = float(samples.iloc[0])

    x1 = first_sample
    x2 = first_sample
    y1 = first_sample
    y2 = first_sample

    filtered = []

    for sample in samples:
        x0 = float(sample)

        y0 = (
            IIR_B0 * x0
            + IIR_B1 * x1
            + IIR_B2 * x2
            - IIR_A1 * y1
            - IIR_A2 * y2
        )

        filtered.append(y0)

        x2 = x1
        x1 = x0

        y2 = y1
        y1 = y0

    return filtered


def add_filtered_currents(averaged_df: pd.DataFrame) -> pd.DataFrame:
    filtered_df = averaged_df.copy()

    filtered_df["IA_mA_iir"] = apply_iir2_lowpass_to_series(filtered_df["IA_mA_avg"])
    filtered_df["IB_mA_iir"] = apply_iir2_lowpass_to_series(filtered_df["IB_mA_avg"])
    filtered_df["IC_mA_iir"] = apply_iir2_lowpass_to_series(filtered_df["IC_mA_avg"])

    return filtered_df


def plot_unfiltered_currents(df: pd.DataFrame, output_path: str | Path | None = None) -> None:
    plt.figure(figsize=(12, 6))

    plt.plot(df["time_s"], df["IA_mA"], label="IA raw", color="red")
    # plt.plot(df["time_s"], df["IB_mA"], label="IB raw", color="blue")
    # plt.plot(df["time_s"], df["IC_mA"], label="IC raw", color="gold")

    plt.xlabel("Time (s)")
    plt.ylabel("Current (mA)")
    plt.title("Unfiltered raw motor phase currents at 1000 Hz")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    if output_path:
        plt.savefig(output_path, dpi=150)
        print(f"Saved unfiltered plot to: {output_path}")

    plt.show()


def plot_averaged_and_filtered_currents(
    df: pd.DataFrame,
    output_path: str | Path | None = None,
) -> None:
    plt.figure(figsize=(12, 6))

    plt.plot(df["time_s"], df["IA_mA_iir"], label="IA avg + IIR", color="red")
    # plt.plot(df["time_s"], df["IB_mA_iir"], label="IB avg + IIR", color="blue")
    # plt.plot(df["time_s"], df["IC_mA_iir"], label="IC avg + IIR", color="gold")

    plt.xlabel("Time (s)")
    plt.ylabel("Current (mA)")
    plt.title("Averaged every 5 samples + 2nd-order 10 Hz Butterworth IIR")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    if output_path:
        plt.savefig(output_path, dpi=150)
        print(f"Saved averaged + filtered plot to: {output_path}")

    plt.show()


def output_paths_from_base(output_path: str | Path | None) -> tuple[Path | None, Path | None]:
    if output_path is None:
        return None, None

    output_path = Path(output_path)
    stem = output_path.stem
    suffix = output_path.suffix if output_path.suffix else ".png"
    parent = output_path.parent

    unfiltered_path = parent / f"{stem}_raw_unfiltered{suffix}"
    filtered_path = parent / f"{stem}_avg5_iir2_10hz{suffix}"

    return unfiltered_path, filtered_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Plot IA, IB, and IC versus time. Then average every 5 samples "
            "and apply a 2nd-order 10 Hz Butterworth IIR filter."
        )
    )
    parser.add_argument("csv_file", help="Path to current log CSV file")
    parser.add_argument(
        "--output",
        "-o",
        help=(
            "Optional base output image path, e.g. logs/current_plot.png. "
            "The script will create *_raw_unfiltered.png and *_avg5_iir2_10hz.png."
        ),
    )
    args = parser.parse_args()

    raw_df = read_current_log(args.csv_file)
    averaged_df = average_every_n_samples(raw_df, AVERAGE_BLOCK_SIZE)
    filtered_df = add_filtered_currents(averaged_df)

    print(f"Loaded raw samples: {len(raw_df)}")
    print(f"Raw sample period: {SAMPLE_PERIOD_SEC} s")
    print(f"Raw sample rate: {1.0 / SAMPLE_PERIOD_SEC:.1f} Hz")
    print(f"Raw duration: {raw_df['time_s'].iloc[-1]:.3f} s")
    print(f"Averaging block size: {AVERAGE_BLOCK_SIZE} samples")
    print(f"Averaged samples: {len(filtered_df)}")
    print(f"Averaged sample period: {AVERAGED_SAMPLE_PERIOD_SEC} s")
    print(f"Averaged sample rate: {AVERAGED_SAMPLE_RATE_HZ:.1f} Hz")
    print("IIR filter: 2nd-order Butterworth low-pass, cutoff = 10 Hz, sample rate = 200 Hz")

    unfiltered_output_path, filtered_output_path = output_paths_from_base(args.output)

    plot_unfiltered_currents(raw_df, unfiltered_output_path)
    plot_averaged_and_filtered_currents(filtered_df, filtered_output_path)


if __name__ == "__main__":
    main()
