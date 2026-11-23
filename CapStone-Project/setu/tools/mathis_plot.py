"""mathis_plot.py - week 7: throughput vs loss on log-log axes with the Mathis prediction.
   CSV columns: loss_pct,run1_mbit,run2_mbit,run3_mbit
   python tools\\mathis_plot.py stack\\measure\\loss.csv --mss 1460 --rtt-ms 40"""
import argparse, csv, math
import matplotlib.pyplot as plt

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('csv'); ap.add_argument('--mss', type=float, default=1460); ap.add_argument('--rtt-ms', type=float, default=40)
    ap.add_argument('--out', default='mathis.png'); a = ap.parse_args()
    rows = list(csv.DictReader(open(a.csv)))
    p = [float(r['loss_pct']) / 100 for r in rows]
    runs = [[float(r[k]) for k in r if k.startswith('run')] for r in rows]
    med = [sorted(x)[len(x) // 2] for x in runs]
    lo = [m - min(x) for m, x in zip(med, runs)]; hi = [max(x) - m for m, x in zip(med, runs)]
    pred = [(a.mss * 8 / (a.rtt_ms / 1000)) * math.sqrt(1.5 / q) / 1e6 for q in p]
    slope = (math.log(med[-1]) - math.log(med[0])) / (math.log(p[-1]) - math.log(p[0]))
    plt.figure(figsize=(7, 4.5))
    plt.errorbar([x * 100 for x in p], med, yerr=[lo, hi], fmt='o-', capsize=4, label='measured (median, range)')
    plt.plot([x * 100 for x in p], pred, '--', label='Mathis: (MSS/RTT)·sqrt(1.5/p)')
    plt.xscale('log'); plt.yscale('log'); plt.xlabel('loss rate (%)'); plt.ylabel('throughput (Mbit/s)')
    plt.title(f'Throughput vs loss - measured slope {slope:.2f} (expected about -0.5)'); plt.grid(True, which='both', alpha=.3); plt.legend()
    plt.tight_layout(); plt.savefig(a.out, dpi=150)
    print(f'slope={slope:.3f}  saved {a.out}')
    for q, m, pr in zip(p, med, pred): print(f'loss {q*100:.3f}%  measured {m:.1f}  predicted {pr:.1f} Mbit/s  gap {100*(m-pr)/pr:+.0f}%')

if __name__ == '__main__':
    main()
