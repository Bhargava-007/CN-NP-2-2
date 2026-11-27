# SETU-LINK Address Plan

## 1. Resource Constraint

The scarce resource in this design is **routing table size and operator comprehension**, not IPv4 address space: `10.20.0.0/16` provides `65,536` addresses (`COMPUTED`), while total 5-year demand is roughly `6,000` addresses (`COMPUTED`).

- **Decision:** Use a hierarchical plan with one `/20` per site and VLSM inside each site block. This yields `4` site summary routes (plus `1` infrastructure summary route = `5` routes total) in the core, instead of roughly `19` individual segment routes under tight VLSM.
- **Alternative rejected:** Tight VLSM across `10.20.0.0/16` without site boundaries (rejected: saves addresses we do not need while fragmenting the routing table into ~19 prefixes).
- **Cost accepted:** Low address efficiency and mostly unused space inside each `/20` and across the `/16`.

## 2. Inputs and Prefix Sizing

- **Growth rule:** `Required = ceil(hosts * 1.6)` over 5 years, with no re-addressing (`COMPUTED`).
- **Usable hosts per prefix (`COMPUTED`):** `/21 = 2046`, `/22 = 1022`, `/23 = 510`, `/24 = 254`, `/25 = 126`, `/26 = 62`, `/27 = 30`. Pick the smallest prefix with `usable >= Required`.
- **Port vs host note:** The spec mentions `~2,400` ports; building port counts below sum to `1,936` (`420 + 780 + 640 + 96`, `COMPUTED`). This plan sizes from the per-segment host counts (`ASSUMPTION`), which sum to `3,226` hosts today (`COMPUTED`).

| Site | Segment | Hosts today (`ASSUMPTION`) | Required (`ceil(hosts * 1.6)`, `COMPUTED`) | Prefix (`COMPUTED`) | Usable (`COMPUTED`) | Headroom (`COMPUTED`) |
| :--- | :--- | ---: | ---: | :--- | ---: | ---: |
| BLD-A (420 ports) | staff | 300 | 480 | `/23` | 510 | 30 |
| BLD-A | student | 20 | 32 | `/26` | 62 | 30 |
| BLD-A | voice | 100 | 160 | `/24` | 254 | 94 |
| BLD-A | mgmt | 30 | 48 | `/26` | 62 | 14 |
| BLD-A | guest | 300 | 480 | `/23` | 510 | 30 |
| BLD-B (780 ports) | staff | 200 | 320 | `/23` | 510 | 190 |
| BLD-B | student | 450 | 720 | `/22` | 1022 | 302 |
| BLD-B | voice | 130 | 208 | `/24` | 254 | 46 |
| BLD-B | mgmt | 60 | 96 | `/25` | 126 | 30 |
| BLD-B | guest | 600 | 960 | `/22` | 1022 | 62 |
| BLD-C (640 ports) | staff | 150 | 240 | `/24` | 254 | 14 |
| BLD-C | student | 250 | 400 | `/23` | 510 | 110 |
| BLD-C | voice | 100 | 160 | `/24` | 254 | 94 |
| BLD-C | instrumentation | 140 | 224 | `/24` | 254 | 30 |
| BLD-C | mgmt | 50 | 80 | `/25` | 126 | 46 |
| BLD-C | guest | 250 | 400 | `/23` | 510 | 110 |
| DC (96 ports) | servers | 60 | 96 | `/25` | 126 | 30 |
| DC | storage | 24 | 39 | `/26` | 62 | 23 |
| DC | mgmt | 12 | 20 | `/27` | 30 | 10 |

All `19` prefix sizes match the expected result (`COMPUTED`).

## 3. IPv4 Site Blocks and Segment Allocations

- **Site blocks:**
  - `BLD-A`: `10.20.0.0/20`
  - `BLD-B`: `10.20.16.0/20`
  - `BLD-C`: `10.20.32.0/20`
  - `DC`: `10.20.48.0/20`
  - `Infrastructure`: `10.20.64.0/20` (point-to-point links as `/31` in `10.20.64.0/24`; loopbacks as `/32` in `10.20.65.0/24`)
  - `Reserved / free`: `10.20.80.0/20` through `10.20.255.255`
- **VLAN IDs (consistent across all buildings):** `10` staff, `20` student, `30` voice, `40` mgmt, `50` guest, `60` instrumentation, `70` servers, `80` storage.
- **Allocation rule:** Largest-first, aligned on prefix boundaries inside each site `/20`. Gateway is the first usable address.

| Site | Segment | VLAN ID | Prefix | Network | First usable | Last usable | Gateway | Required (`COMPUTED`) | Usable (`COMPUTED`) | Headroom (`COMPUTED`) |
| :--- | :--- | ---: | :--- | :--- | :--- | :--- | :--- | ---: | ---: | ---: |
| BLD-A | staff | 10 | `/23` | `10.20.0.0/23` | `10.20.0.1` | `10.20.1.254` | `10.20.0.1` | 480 | 510 | 30 |
| BLD-A | guest | 50 | `/23` | `10.20.2.0/23` | `10.20.2.1` | `10.20.3.254` | `10.20.2.1` | 480 | 510 | 30 |
| BLD-A | voice | 30 | `/24` | `10.20.4.0/24` | `10.20.4.1` | `10.20.4.254` | `10.20.4.1` | 160 | 254 | 94 |
| BLD-A | student | 20 | `/26` | `10.20.5.0/26` | `10.20.5.1` | `10.20.5.62` | `10.20.5.1` | 32 | 62 | 30 |
| BLD-A | mgmt | 40 | `/26` | `10.20.5.64/26` | `10.20.5.65` | `10.20.5.126` | `10.20.5.65` | 48 | 62 | 14 |
| BLD-B | student | 20 | `/22` | `10.20.16.0/22` | `10.20.16.1` | `10.20.19.254` | `10.20.16.1` | 720 | 1022 | 302 |
| BLD-B | guest | 50 | `/22` | `10.20.20.0/22` | `10.20.20.1` | `10.20.23.254` | `10.20.20.1` | 960 | 1022 | 62 |
| BLD-B | staff | 10 | `/23` | `10.20.24.0/23` | `10.20.24.1` | `10.20.25.254` | `10.20.24.1` | 320 | 510 | 190 |
| BLD-B | voice | 30 | `/24` | `10.20.26.0/24` | `10.20.26.1` | `10.20.26.254` | `10.20.26.1` | 208 | 254 | 46 |
| BLD-B | mgmt | 40 | `/25` | `10.20.27.0/25` | `10.20.27.1` | `10.20.27.126` | `10.20.27.1` | 96 | 126 | 30 |
| BLD-C | student | 20 | `/23` | `10.20.32.0/23` | `10.20.32.1` | `10.20.33.254` | `10.20.32.1` | 400 | 510 | 110 |
| BLD-C | guest | 50 | `/23` | `10.20.34.0/23` | `10.20.34.1` | `10.20.35.254` | `10.20.34.1` | 400 | 510 | 110 |
| BLD-C | staff | 10 | `/24` | `10.20.36.0/24` | `10.20.36.1` | `10.20.36.254` | `10.20.36.1` | 240 | 254 | 14 |
| BLD-C | voice | 30 | `/24` | `10.20.37.0/24` | `10.20.37.1` | `10.20.37.254` | `10.20.37.1` | 160 | 254 | 94 |
| BLD-C | instrumentation | 60 | `/24` | `10.20.38.0/24` | `10.20.38.1` | `10.20.38.254` | `10.20.38.1` | 224 | 254 | 30 |
| BLD-C | mgmt | 40 | `/25` | `10.20.39.0/25` | `10.20.39.1` | `10.20.39.126` | `10.20.39.1` | 80 | 126 | 46 |
| DC | servers | 70 | `/25` | `10.20.48.0/25` | `10.20.48.1` | `10.20.48.126` | `10.20.48.1` | 96 | 126 | 30 |
| DC | storage | 80 | `/26` | `10.20.48.128/26` | `10.20.48.129` | `10.20.48.190` | `10.20.48.129` | 39 | 62 | 23 |
| DC | mgmt | 40 | `/27` | `10.20.48.192/27` | `10.20.48.193` | `10.20.48.222` | `10.20.48.193` | 20 | 30 | 10 |

## 4. IPv6 Plan

- **Global prefix:** `2001:db8:5e70::/48`
- **Site `/52` blocks:**
  - `BLD-A`: `2001:db8:5e70:0000::/52`
  - `BLD-B`: `2001:db8:5e70:1000::/52`
  - `BLD-C`: `2001:db8:5e70:2000::/52`
  - `DC`: `2001:db8:5e70:3000::/52`
  - `Infrastructure`: `2001:db8:5e70:f000::/52`
- **Rule:** One `/64` per segment, staying inside the site `/52` by setting the `/64` hextet to `site_base + VLAN_ID_hex` (e.g., `2001:db8:5e70:0010::/64` breaks the `/52` boundary if used for BLD-A when VLAN ID is treated as decimal, whereas `site_base + VLAN_ID` keeps every segment inside its `/52`, e.g., BLD-B staff VLAN 10 = `2001:db8:5e70:100a::/64`).

| Site | Segment | VLAN ID (dec / hex) | IPv6 `/64` Prefix |
| :--- | :--- | :--- | :--- |
| BLD-A | staff | 10 / `0a` | `2001:db8:5e70:000a::/64` |
| BLD-A | student | 20 / `14` | `2001:db8:5e70:0014::/64` |
| BLD-A | voice | 30 / `1e` | `2001:db8:5e70:001e::/64` |
| BLD-A | mgmt | 40 / `28` | `2001:db8:5e70:0028::/64` |
| BLD-A | guest | 50 / `32` | `2001:db8:5e70:0032::/64` |
| BLD-B | staff | 10 / `0a` | `2001:db8:5e70:100a::/64` |
| BLD-B | student | 20 / `14` | `2001:db8:5e70:1014::/64` |
| BLD-B | voice | 30 / `1e` | `2001:db8:5e70:101e::/64` |
| BLD-B | mgmt | 40 / `28` | `2001:db8:5e70:1028::/64` |
| BLD-B | guest | 50 / `32` | `2001:db8:5e70:1032::/64` |
| BLD-C | staff | 10 / `0a` | `2001:db8:5e70:200a::/64` |
| BLD-C | student | 20 / `14` | `2001:db8:5e70:2014::/64` |
| BLD-C | voice | 30 / `1e` | `2001:db8:5e70:201e::/64` |
| BLD-C | mgmt | 40 / `28` | `2001:db8:5e70:2028::/64` |
| BLD-C | guest | 50 / `32` | `2001:db8:5e70:2032::/64` |
| BLD-C | instrumentation | 60 / `3c` | `2001:db8:5e70:203c::/64` |
| DC | mgmt | 40 / `28` | `2001:db8:5e70:3028::/64` |
| DC | servers | 70 / `46` | `2001:db8:5e70:3046::/64` |
| DC | storage | 80 / `50` | `2001:db8:5e70:3050::/64` |

## 5. Address Efficiency (`COMPUTED`)

Per-segment efficiency (`hosts_today / usable` and `hosts_5y / usable`):

| Site | Segment | Today (`hosts_today / usable`) | 5-Year (`hosts_5y / usable`) |
| :--- | :--- | :--- | :--- |
| BLD-A | staff | `300 / 510 = 58.8%` | `480 / 510 = 94.1%` |
| BLD-A | student | `20 / 62 = 32.3%` | `32 / 62 = 51.6%` |
| BLD-A | voice | `100 / 254 = 39.4%` | `160 / 254 = 63.0%` |
| BLD-A | mgmt | `30 / 62 = 48.4%` | `48 / 62 = 77.4%` |
| BLD-A | guest | `300 / 510 = 58.8%` | `480 / 510 = 94.1%` |
| BLD-B | staff | `200 / 510 = 39.2%` | `320 / 510 = 62.7%` |
| BLD-B | student | `450 / 1022 = 44.0%` | `720 / 1022 = 70.5%` |
| BLD-B | voice | `130 / 254 = 51.2%` | `208 / 254 = 81.9%` |
| BLD-B | mgmt | `60 / 126 = 47.6%` | `96 / 126 = 76.2%` |
| BLD-B | guest | `600 / 1022 = 58.7%` | `960 / 1022 = 93.9%` |
| BLD-C | staff | `150 / 254 = 59.1%` | `240 / 254 = 94.5%` |
| BLD-C | student | `250 / 510 = 49.0%` | `400 / 510 = 78.4%` |
| BLD-C | voice | `100 / 254 = 39.4%` | `160 / 254 = 63.0%` |
| BLD-C | instrumentation | `140 / 254 = 55.1%` | `224 / 254 = 88.2%` |
| BLD-C | mgmt | `50 / 126 = 39.7%` | `80 / 126 = 63.5%` |
| BLD-C | guest | `250 / 510 = 49.0%` | `400 / 510 = 78.4%` |
| DC | servers | `60 / 126 = 47.6%` | `96 / 126 = 76.2%` |
| DC | storage | `24 / 62 = 38.7%` | `39 / 62 = 62.9%` |
| DC | mgmt | `12 / 30 = 40.0%` | `20 / 30 = 66.7%` |

- **Per-site efficiency (`sum(hosts) / 4096`, `COMPUTED`):**
  - `BLD-A`: `750 / 4096 = 18.3%` today; `1200 / 4096 = 29.3%` at 5 years.
  - `BLD-B`: `1440 / 4096 = 35.2%` today; `2304 / 4096 = 56.3%` at 5 years.
  - `BLD-C`: `940 / 4096 = 22.9%` today; `1504 / 4096 = 36.7%` at 5 years.
  - `DC`: `96 / 4096 = 2.3%` today; `155 / 4096 = 3.8%` at 5 years.
- **Overall `/16` efficiency (`COMPUTED`):**
  - Today: `3226 / 65536 = 4.9%`.
  - 5-year: `5163 / 65536 = 7.9%`.
- **Honest interpretation:** `4.9%` today (`7.9%` at 5 years) is intentionally low. In private RFC 1918 space (`10.20.0.0/16`), trading unused addresses for clean `/20` site boundaries and a 5-route core table is the right trade-off.

## 6. Summaries

The entire core routing table for addressing consists of **5 summary routes**:
1. `10.20.0.0/20` (`BLD-A`)
2. `10.20.16.0/20` (`BLD-B`)
3. `10.20.32.0/20` (`BLD-C`)
4. `10.20.48.0/20` (`DC`)
5. `10.20.64.0/20` (`Infrastructure`: `/31` links and `/32` loopbacks)

Because every segment stays strictly inside its site `/20`, the core never needs to carry the 19 individual segment prefixes, keeping routing, ACLs, and troubleshooting simple.

## 7. Open Items

- Reserve the first `10` usable addresses of each segment for static assignments, gateway, and HSRP/VRRP.
- Guest isolation is handled in `POLICY.md`.
- Existing lab-kit addresses (`10.20.40.0/24`, etc.) are NOT part of this plan and will be reconciled in step 3.

## 8. Reverse DNS Zones

### IPv4 (`in-addr.arpa`)

| Segment | Prefix | Reverse zone(s) needed | Delegation note | Status |
| :--- | :--- | :--- | :--- | :--- |
| BLD-A staff | `10.20.0.0/23` | `0.20.10.in-addr.arpa`, `1.20.10.in-addr.arpa` | Two `/24` zones on octet boundary | planned, not tested |
| BLD-A guest | `10.20.2.0/23` | `2.20.10.in-addr.arpa`, `3.20.10.in-addr.arpa` | Two `/24` zones on octet boundary | planned, not tested |
| BLD-A voice | `10.20.4.0/24` | `4.20.10.in-addr.arpa` | Single `/24` zone | planned, not tested |
| BLD-A student | `10.20.5.0/26` | `5.20.10.in-addr.arpa` | RFC 2317 classless delegation (`0-63.5.20.10.in-addr.arpa` CNAME-range style) | planned, not tested |
| BLD-A mgmt | `10.20.5.64/26` | `5.20.10.in-addr.arpa` | RFC 2317 classless delegation (`64-127.5.20.10.in-addr.arpa` CNAME-range style) | planned, not tested |
| BLD-B student | `10.20.16.0/22` | `16.20.10.in-addr.arpa` .. `19.20.10.in-addr.arpa` | Four `/24` zones on octet boundary | planned, not tested |
| BLD-B guest | `10.20.20.0/22` | `20.20.10.in-addr.arpa` .. `23.20.10.in-addr.arpa` | Four `/24` zones on octet boundary | planned, not tested |
| BLD-B staff | `10.20.24.0/23` | `24.20.10.in-addr.arpa`, `25.20.10.in-addr.arpa` | Two `/24` zones on octet boundary | planned, not tested |
| BLD-B voice | `10.20.26.0/24` | `26.20.10.in-addr.arpa` | Single `/24` zone | planned, not tested |
| BLD-B mgmt | `10.20.27.0/25` | `27.20.10.in-addr.arpa` | RFC 2317 classless delegation (`0-127.27.20.10.in-addr.arpa` CNAME-range style) | planned, not tested |
| BLD-C student | `10.20.32.0/23` | `32.20.10.in-addr.arpa`, `33.20.10.in-addr.arpa` | Two `/24` zones on octet boundary | planned, not tested |
| BLD-C guest | `10.20.34.0/23` | `34.20.10.in-addr.arpa`, `35.20.10.in-addr.arpa` | Two `/24` zones on octet boundary | planned, not tested |
| BLD-C staff | `10.20.36.0/24` | `36.20.10.in-addr.arpa` | Single `/24` zone | planned, not tested |
| BLD-C voice | `10.20.37.0/24` | `37.20.10.in-addr.arpa` | Single `/24` zone | planned, not tested |
| BLD-C instrumentation | `10.20.38.0/24` | `38.20.10.in-addr.arpa` | Single `/24` zone | planned, not tested |
| BLD-C mgmt | `10.20.39.0/25` | `39.20.10.in-addr.arpa` | RFC 2317 classless delegation (`0-127.39.20.10.in-addr.arpa` CNAME-range style) | planned, not tested |
| DC servers | `10.20.48.0/25` | `48.20.10.in-addr.arpa` | Parent `/24` zone; RFC 2317 (`0-127.48.20.10.in-addr.arpa`) if split | built in `link\config\db.48.20.10.in-addr.arpa` |
| DC storage | `10.20.48.128/26` | `48.20.10.in-addr.arpa` | RFC 2317 classless delegation (`128-191.48.20.10.in-addr.arpa` CNAME-range style) | planned, not tested |
| DC mgmt | `10.20.48.192/27` | `48.20.10.in-addr.arpa` | RFC 2317 classless delegation (`192-223.48.20.10.in-addr.arpa` CNAME-range style) | planned, not tested |
| Infra links | `10.20.64.0/24` (`/31`s) | `64.20.10.in-addr.arpa` | Parent `/24` zone; RFC 2317 CNAME-range style if delegated per `/31` | planned, not tested |
| Infra loopbacks | `10.20.65.0/24` (`/32`s) | `65.20.10.in-addr.arpa` | Parent `/24` zone | planned, not tested |

### IPv6 (`ip6.arpa`)

- Global `/48` (`2001:db8:5e70::/48`): `0.7.e.5.8.b.d.0.1.0.0.2.ip6.arpa` (12 nibble labels + `ip6.arpa` = 14 labels; planned, not tested).
- Site `/52` zones (nibble-aligned, 13 nibble labels + `ip6.arpa` = 15 labels; all planned, not tested):
  - `BLD-A` (`2001:db8:5e70:0000::/52`): `0.0.7.e.5.8.b.d.0.1.0.0.2.ip6.arpa`
  - `BLD-B` (`2001:db8:5e70:1000::/52`): `1.0.7.e.5.8.b.d.0.1.0.0.2.ip6.arpa`
  - `BLD-C` (`2001:db8:5e70:2000::/52`): `2.0.7.e.5.8.b.d.0.1.0.0.2.ip6.arpa`
  - `DC` (`2001:db8:5e70:3000::/52`): `3.0.7.e.5.8.b.d.0.1.0.0.2.ip6.arpa`
  - `Infrastructure` (`2001:db8:5e70:f000::/52`): `f.0.7.e.5.8.b.d.0.1.0.0.2.ip6.arpa`

### What Is Already Built

- `48.20.10.in-addr.arpa` exists for DC servers (`10.20.48.0/25`) in `link\config\db.48.20.10.in-addr.arpa`.
- All other IPv4 and IPv6 reverse zones are planned, not created.
- No reverse zone other than `48.20.10.in-addr.arpa` has been tested.
