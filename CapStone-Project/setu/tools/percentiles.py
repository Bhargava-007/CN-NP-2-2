"""percentiles.py - p50/p95/p99 from a column of numbers (never just the mean).
   PowerShell:  python tools\\percentiles.py edge\\measure\\run2.csv --column latency_ms"""
import argparse, csv, math, sys

def pct(sorted_vals, p):
    if not sorted_vals: return float('nan')
    rank = max(1, math.ceil(p / 100 * len(sorted_vals)))      # nearest-rank, same as Get-SetuPercentile
    return sorted_vals[rank - 1]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file', nargs='?', help='CSV file (default: numbers on stdin)')
    ap.add_argument('--column', default=None, help='CSV column name')
    a = ap.parse_args()
    if a.file:
        with open(a.file, newline='') as f:
            rows = list(csv.DictReader(f)) if a.column else [[x] for x in f.read().split()]
        vals = [float(r[a.column]) for r in rows] if a.column else [float(r[0]) for r in rows]
    else:
        vals = [float(x) for x in sys.stdin.read().split()]
    v = sorted(vals)
    mean = sum(v) / len(v)
    print(f"n={len(v)}  mean={mean:.3f}  p50={pct(v,50):.3f}  p95={pct(v,95):.3f}  p99={pct(v,99):.3f}  "
          f"p99.9={pct(v,99.9):.3f}  max={v[-1]:.3f}  jitter(p99-p50)={pct(v,99)-pct(v,50):.3f}")

if __name__ == '__main__':
    main()
