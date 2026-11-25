#!/usr/bin/env python3
"""Checker for link/design/ADDRESS-PLAN.md."""

import ipaddress
import math
import re
import sys
from pathlib import Path

SITE_V4 = {
    "BLD-A": ipaddress.IPv4Network("10.20.0.0/20"),
    "BLD-B": ipaddress.IPv4Network("10.20.16.0/20"),
    "BLD-C": ipaddress.IPv4Network("10.20.32.0/20"),
    "DC": ipaddress.IPv4Network("10.20.48.0/20"),
}

SITE_V6 = {
    "BLD-A": ipaddress.IPv6Network("2001:db8:5e70:0000::/52"),
    "BLD-B": ipaddress.IPv6Network("2001:db8:5e70:1000::/52"),
    "BLD-C": ipaddress.IPv6Network("2001:db8:5e70:2000::/52"),
    "DC": ipaddress.IPv6Network("2001:db8:5e70:3000::/52"),
}

GLOBAL_V6 = ipaddress.IPv6Network("2001:db8:5e70::/48")


def strip_ticks(s: str) -> str:
    return s.strip().strip("`").strip()


def parse_tables(text: str) -> list[list[list[str]]]:
    tables = []
    cur = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("|") and s.endswith("|"):
            cells = [c.strip() for c in s[1:-1].split("|")]
            cur.append(cells)
        else:
            if cur:
                tables.append(cur)
                cur = []
    if cur:
        tables.append(cur)
    return tables


def data_rows(table: list[list[str]]) -> list[list[str]]:
    # Skip header and separator row
    return [r for r in table[2:] if r and not all(set(c) <= set("-: ") for c in r)]


def main() -> int:
    plan_path = Path(__file__).resolve().parents[1] / "design" / "ADDRESS-PLAN.md"
    if len(sys.argv) > 1:
        plan_path = Path(sys.argv[1])
    if not plan_path.exists():
        print(f"FAIL: missing {plan_path}")
        return 1

    text = plan_path.read_text(encoding="utf-8")
    tables = parse_tables(text)
    if len(tables) < 4:
        print(f"FAIL: expected at least 4 markdown tables, found {len(tables)}")
        return 1

    inputs_tbl = data_rows(tables[0])
    v4_tbl = data_rows(tables[1])
    v6_tbl = data_rows(tables[2])
    eff_tbl = data_rows(tables[3])

    failures = 0

    def record(ok: bool, name: str, detail: str) -> None:
        nonlocal failures
        status = "PASS" if ok else "FAIL"
        if not ok:
            failures += 1
        print(f"{status}: {name} - {detail}")

    # Parse inputs: (site, segment) -> hosts_today
    hosts_map: dict[tuple[str, str], int] = {}
    for row in inputs_tbl:
        site = re.sub(r"\s*\(.*\)$", "", strip_ticks(row[0]))
        seg = strip_ticks(row[1])
        hosts_map[(site, seg)] = int(strip_ticks(row[2]))

    # Parse IPv4 allocations
    v4_entries = []
    for row in v4_tbl:
        site = strip_ticks(row[0])
        seg = strip_ticks(row[1])
        vlan = int(strip_ticks(row[2]))
        net = ipaddress.IPv4Network(strip_ticks(row[4]), strict=True)
        gw = ipaddress.IPv4Address(strip_ticks(row[7]))
        v4_entries.append((site, seg, vlan, net, gw))

    # 1. Every IPv4 segment prefix lies inside its site /20 and is aligned.
    bad_site = []
    for site, seg, _vlan, net, _gw in v4_entries:
        if site not in SITE_V4 or not net.subnet_of(SITE_V4[site]):
            bad_site.append(f"{site}/{seg} {net}")
    record(len(bad_site) == 0, "1. IPv4 site containment & alignment",
           "all segments inside site /20" if not bad_site else "; ".join(bad_site))

    # 2. No two IPv4 prefixes overlap (segments + infra /31 links & /32 loopbacks).
    infra_nets = [
        ipaddress.IPv4Network("10.20.64.0/24"),
        ipaddress.IPv4Network("10.20.65.0/24"),
    ]
    all_v4 = [e[3] for e in v4_entries] + infra_nets
    overlaps_v4 = []
    for i in range(len(all_v4)):
        for j in range(i + 1, len(all_v4)):
            if all_v4[i].overlaps(all_v4[j]):
                overlaps_v4.append(f"{all_v4[i]} vs {all_v4[j]}")
    record(len(overlaps_v4) == 0, "2. IPv4 no overlaps",
           f"{len(all_v4)} prefixes disjoint" if not overlaps_v4 else "; ".join(overlaps_v4))

    # 3. Every IPv6 /64 lies inside its site /52 and inside 2001:db8:5e70::/48; no overlaps.
    v6_entries = []
    bad_v6 = []
    for row in v6_tbl:
        site = strip_ticks(row[0])
        seg = strip_ticks(row[1])
        vlan_dec = int(row[2].split("/")[0].strip())
        net6 = ipaddress.IPv6Network(strip_ticks(row[3]), strict=True)
        v6_entries.append((site, seg, vlan_dec, net6))
        if net6.prefixlen != 64 or not net6.subnet_of(GLOBAL_V6) or site not in SITE_V6 or not net6.subnet_of(SITE_V6[site]):
            bad_v6.append(f"{site}/{seg} {net6}")
    overlaps_v6 = []
    for i in range(len(v6_entries)):
        for j in range(i + 1, len(v6_entries)):
            if v6_entries[i][3].overlaps(v6_entries[j][3]):
                overlaps_v6.append(f"{v6_entries[i][3]} vs {v6_entries[j][3]}")
    record(len(bad_v6) == 0 and len(overlaps_v6) == 0, "3. IPv6 containment & no overlaps",
           f"{len(v6_entries)} /64s valid & disjoint" if (not bad_v6 and not overlaps_v6)
           else "; ".join(bad_v6 + overlaps_v6))

    # 4. Usable >= ceil(hosts*1.6) and smallest prefix that fits.
    bad_size = []
    for site, seg, _vlan, net, _gw in v4_entries:
        hosts = hosts_map[(site, seg)]
        req = math.ceil(hosts * 1.6)
        usable = (1 << (32 - net.prefixlen)) - 2
        smaller_usable = (1 << (32 - (net.prefixlen + 1))) - 2
        if usable < req or smaller_usable >= req:
            bad_size.append(f"{site}/{seg}: req={req}, /{net.prefixlen} usable={usable}")
    record(len(bad_size) == 0, "4. Smallest prefix sizing",
           f"all {len(v4_entries)} segments minimal" if not bad_size else "; ".join(bad_size))

    # 5. Gateway = first usable address of its prefix.
    bad_gw = []
    for site, seg, _vlan, net, gw in v4_entries:
        first_usable = net.network_address + 1
        if gw != first_usable:
            bad_gw.append(f"{site}/{seg}: gw={gw}, expected={first_usable}")
    record(len(bad_gw) == 0, "5. Gateway is first usable",
           "all gateways match" if not bad_gw else "; ".join(bad_gw))

    # 6. VLAN IDs consistent across buildings; IPv6 /64 index matches site_base + VLAN_ID.
    vlan_role: dict[int, str] = {}
    bad_vlan = []
    for site, seg, vlan, _net, _gw in v4_entries:
        if vlan in vlan_role and vlan_role[vlan] != seg:
            bad_vlan.append(f"VLAN {vlan}: {vlan_role[vlan]} vs {seg}")
        vlan_role[vlan] = seg
    for site, seg, vlan_dec, net6 in v6_entries:
        if vlan_role.get(vlan_dec) != seg:
            bad_vlan.append(f"IPv6 VLAN {vlan_dec} role mismatch: {seg}")
        site_base_int = int(SITE_V6[site].network_address) >> 64
        actual_hextet = int(net6.network_address) >> 64
        if actual_hextet != site_base_int + vlan_dec:
            bad_vlan.append(f"{site}/{seg}: got {actual_hextet:04x}, expected {site_base_int + vlan_dec:04x}")
    record(len(bad_vlan) == 0, "6. VLAN consistency & IPv6 index rule",
           "all VLANs and IPv6 /64 indices match" if not bad_vlan else "; ".join(bad_vlan))

    # 7. Efficiency check (tolerance 0.1 percentage point).
    bad_eff = []
    v4_by_key = {(s, g): n for s, g, _v, n, _gw in v4_entries}
    for row in eff_tbl:
        site = strip_ticks(row[0])
        seg = strip_ticks(row[1])
        hosts = hosts_map[(site, seg)]
        req = math.ceil(hosts * 1.6)
        usable = (1 << (32 - v4_by_key[(site, seg)].prefixlen)) - 2
        exp_t = 100.0 * hosts / usable
        exp_5 = 100.0 * req / usable
        m_t = re.search(r"=\s*([0-9.]+)%", row[2])
        m_5 = re.search(r"=\s*([0-9.]+)%", row[3])
        if not m_t or not m_5:
            bad_eff.append(f"{site}/{seg}: parse error")
            continue
        if abs(float(m_t.group(1)) - exp_t) > 0.1 or abs(float(m_5.group(1)) - exp_5) > 0.1:
            bad_eff.append(f"{site}/{seg}: got {m_t.group(1)}%/{m_5.group(1)}%, exp {exp_t:.1f}%/{exp_5:.1f}%")

    for site in SITE_V4:
        s_today = sum(h for (st, _), h in hosts_map.items() if st == site)
        s_5y = sum(math.ceil(h * 1.6) for (st, _), h in hosts_map.items() if st == site)
        m = re.search(rf"`{re.escape(site)}`:\s*`\d+\s*/\s*4096\s*=\s*([0-9.]+)%`\s*today;\s*`\d+\s*/\s*4096\s*=\s*([0-9.]+)%`", text)
        if not m:
            bad_eff.append(f"{site} site efficiency not found")
        else:
            if abs(float(m.group(1)) - 100.0 * s_today / 4096) > 0.1 or abs(float(m.group(2)) - 100.0 * s_5y / 4096) > 0.1:
                bad_eff.append(f"{site} site efficiency mismatch")

    tot_today = sum(hosts_map.values())
    tot_5y = sum(math.ceil(h * 1.6) for h in hosts_map.values())
    m_ot = re.search(r"Today:\s*`\d+\s*/\s*65536\s*=\s*([0-9.]+)%`", text)
    m_o5 = re.search(r"5-year:\s*`\d+\s*/\s*65536\s*=\s*([0-9.]+)%`", text)
    if not m_ot or not m_o5:
        bad_eff.append("overall efficiency not found")
    else:
        if abs(float(m_ot.group(1)) - 100.0 * tot_today / 65536) > 0.1 or abs(float(m_o5.group(1)) - 100.0 * tot_5y / 65536) > 0.1:
            bad_eff.append("overall efficiency mismatch")

    record(len(bad_eff) == 0, "7. Efficiency arithmetic",
           "all segment, site, and overall efficiencies within 0.1 pp" if not bad_eff else "; ".join(bad_eff))

    print(f"SUMMARY: {7 - failures}/7 checks passed, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
