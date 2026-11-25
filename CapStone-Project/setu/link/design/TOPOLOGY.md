# SETU-LINK Topology (`TOPOLOGY.md`)

## 1. Sites and Buildings

| Site | Role | Ports (`ADDRESS-PLAN.md`) | IPv4 Block | IPv6 Block |
| :--- | :--- | ---: | :--- | :--- |
| BLD-A | Admin | 420 | `10.20.0.0/20` | `2001:db8:5e70:0000::/52` |
| BLD-B | Academic | 780 | `10.20.16.0/20` | `2001:db8:5e70:1000::/52` |
| BLD-C | Labs | 640 | `10.20.32.0/20` | `2001:db8:5e70:2000::/52` |
| DC | Data Center | 96 | `10.20.48.0/20` | `2001:db8:5e70:3000::/52` |

Infrastructure block: `10.20.64.0/20` (`10.20.64.0/24` `/31` links, `10.20.65.0/24` `/32` loopbacks; `2001:db8:5e70:f000::/52`).

## 2. Logical Layers

- **Access:** Per-building access switches (`sw-*`) connecting end hosts and segments.
- **Building router / L3:** Per-building L3 boundary (`rtr`) terminating segment gateways.
- **Core:** Inter-building and DC transit (`TODO`: multi-router OSPF core).
- **DC:** Server, storage, and management segment (`10.20.48.0/20`).
- **Upstream:** One upstream provider now (`TODO`); a second upstream provider in week 8 (`TODO`).

```text
[Access: BLD-A/B/C] --- [Building L3 / rtr] --- [Core (TODO)] --- [DC]
                                                      |
                                             [Upstream 1 / 2 (TODO)]
```

## 3. Lab Status Table

| Topology | Status | Notes |
| :--- | :--- | :--- |
| `w01` | Built and tested | Week 1 baseline (`dev01` - `rtr` - `srv01`) |
| `w03` (`w03-services.clab.yml`) | Built and tested | Week 3 DHCP relay + DNS + `srv01` |
| `w08-ospf` | NOT BUILT | `TODO` (week 8) |
| `w08-edge` | NOT BUILT | `TODO` (week 8) |
| `w09-switching` | NOT BUILT | `TODO` (week 9) |
| `w12-qos` | NOT BUILT | `TODO` (week 12) |

## 4. What `w03-services.clab.yml` Actually Contains

`w03-services.clab.yml` (`setu-w03`) is a reduced model of one access segment (`BLD-C` student `10.20.32.0/23`), one router (`rtr`), and DC servers (`10.20.48.0/25`):

- **Nodes & addresses:**
  - `sw-c` (`nicolaka/netshoot`): access bridge for `dev01` and `rtr:eth1`.
  - `rtr` (`frrouting/frr:latest`): `eth1` `10.20.32.1/23`, `eth2` `10.20.48.1/25`, `ip_forward=1`.
  - `sw-dc` (`nicolaka/netshoot`): DC bridge for `rtr:eth2`, `srv01`, `kea01`, `ns1`.
  - `dev01` (`nicolaka/netshoot`): DHCP client on `eth1`.
  - `srv01` (`nicolaka/netshoot`): `10.20.48.8/25`, default via `10.20.48.1`, port `8080:8080`.
  - `kea01` (`debian:bookworm-slim`): `10.20.48.9/25` (Kea 2.2.0 started by hand after deploy; idles on `sleep infinity`).
  - `ns1` (`ubuntu/bind9:latest`): `10.20.48.10/25`, default via `10.20.48.1`, port `5353:53/udp`.
- **Links:** `dev01:eth1 - sw-c:eth1`, `sw-c:eth2 - rtr:eth1`, `rtr:eth2 - sw-dc:eth1`, `sw-dc:eth2 - srv01:eth1`, `sw-dc:eth3 - kea01:eth1`, `sw-dc:eth4 - ns1:eth1`.

## 5. TODO List for Later Weeks

- `TODO`: VLAN trunks across access/distribution
- `TODO`: STP (Spanning Tree Protocol)
- `TODO`: LACP (link aggregation)
- `TODO`: OSPF (week 8)
- `TODO`: NAT and second uplink (week 8)
- `TODO`: Wireless
- `TODO`: Monitoring
