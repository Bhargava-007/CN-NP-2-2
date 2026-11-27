#!/usr/bin/env python3
"""
Purpose: Plot CP2 concurrency cost curves (RPS and p99 latency vs idle connections N)
         using median and min-max error bars across runs.
Usage:   python3 stack/measure/plot_costcurve.py <input.txt> <output.png>
Status:  written, not run.
"""

import re
import statistics
import sys
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

LINE_RE = re.compile(
    r"^(?P<model>\S+)\s+N=(?P<N>\d+)\s+run=(?P<run>\d+):.*?"
    r"\brps=(?P<rps>[0-9.]+)\b.*?\bp99=(?P<p99>[0-9.]+)\b"
)


def parse_file(path: str) -> dict[str, dict[int, list[tuple[float, float]]]]:
    data: dict[str, dict[int, list[tuple[float, float]]]] = defaultdict(lambda: defaultdict(list))
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            m = LINE_RE.match(line.strip())
            if not m:
                continue
            model = m.group("model")
            n_val = int(m.group("N"))
            rps = float(m.group("rps"))
            p99 = float(m.group("p99"))
            data[model][n_val].append((rps, p99))
    return data


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("Usage: plot_costcurve.py <input.txt> <output.png>\n")
        return 2

    in_path, out_path = sys.argv[1], sys.argv[2]
    data = parse_file(in_path)
    if not data:
        sys.stderr.write(f"No matching data lines found in {in_path}\n")
        return 1

    fig, (ax_rps, ax_p99) = plt.subplots(1, 2, figsize=(11, 4.8))
    fig.suptitle(
        "CP2 Concurrency Cost Curves (loopback, release -O2, 50 active, K=1, 3 runs)",
        fontsize=12,
    )

    order = ["epoll", "poll", "select"]
    models = [m for m in order if m in data] + [m for m in sorted(data) if m not in order]

    for model in models:
        ns = sorted(data[model].keys())
        if model == "select":
            ns = [n for n in ns if n <= 500]

        rps_med, rps_lo, rps_hi = [], [], []
        p99_med, p99_lo, p99_hi = [], [], []

        for n in ns:
            runs = data[model][n]
            r_vals = [x[0] for x in runs]
            p_vals = [x[1] for x in runs]

            rm = statistics.median(r_vals)
            pm = statistics.median(p_vals)

            rps_med.append(rm)
            rps_lo.append(rm - min(r_vals))
            rps_hi.append(max(r_vals) - rm)

            p99_med.append(pm)
            p99_lo.append(pm - min(p_vals))
            p99_hi.append(max(p_vals) - pm)

        ax_rps.errorbar(
            ns, rps_med, yerr=[rps_lo, rps_hi],
            marker="o", capsize=4, label=model
        )
        ax_p99.errorbar(
            ns, p99_med, yerr=[p99_lo, p99_hi],
            marker="o", capsize=4, label=model
        )

    ax_rps.set_xscale("log")
    ax_rps.set_xlabel("Idle connections N (count, log scale)")
    ax_rps.set_ylabel("Throughput (requests/s)")
    ax_rps.set_title("(a) Throughput vs N")
    ax_rps.grid(True, which="both", linestyle="--", alpha=0.4)
    ax_rps.legend()

    ax_p99.set_xscale("log")
    ax_p99.set_xlabel("Idle connections N (count, log scale)")
    ax_p99.set_ylabel("p99 Latency (ms)")
    ax_p99.set_title("(b) p99 Latency vs N")
    ax_p99.grid(True, which="both", linestyle="--", alpha=0.4)
    ax_p99.legend()

    fig.tight_layout(rect=[0, 0, 1, 0.93])
    fig.savefig(out_path, dpi=150)
    return 0


if __name__ == "__main__":
    sys.exit(main())
