# Week 1 Field Diff (`W01-FIELD-DIFF`)

## UDP request

| Field | tap1 `dev01:eth1` | tap2 `rtr:eth1` | tap3 `srv01:eth1` |
| :--- | :--- | :--- | :--- |
| `eth.src` | `aa:c1:ab:32:48:a8` | `aa:c1:ab:32:48:a8` | `aa:c1:ab:c0:5f:ad` |
| `eth.dst` | `aa:c1:ab:5d:34:33` | `aa:c1:ab:5d:34:33` | `aa:c1:ab:13:cc:87` |
| `vlan` | none | none | none |
| `ip.ttl` | 64 | 64 | 63 |
| `ip.checksum` | `0x6428` | `0x6428` | `0x6528` |
| `frame.len` | 53 | 53 | 53 |

## ICMP port-unreachable reply

| Field | `dev01`-side | `srv01`-side |
| :--- | :--- | :--- |
| Outer `ip.ttl` | 63 | 64 |
| Outer `ip.checksum` | `0x0960` | `0x0860` |
| `frame.len` | 81 | 81 |
| Inner `ip.ttl` | 63 | 63 |
| Inner `ip.checksum` | `0x6528` | `0x6528` |

## Observations

- **MEASURED:** Switches change nothing at L2/L3/L4 (`tap1` `dev01:eth1` and `tap2` `rtr:eth1` are identical across all observed fields).
- **MEASURED:** Router (`rtr`) rewrites both MACs (`eth.src` and `eth.dst`), decrements `ip.ttl` by 1 (`64` -> `63`), and increments `ip.checksum` by `+0x0100` (`0x6428` -> `0x6528`).
- **MEASURED:** No 802.1Q tag in this topology (bridges are not VLAN-filtering), so tag-drop is untested.
- **COMPUTED:** Wire efficiency for the 11-byte UDP payload: `11/53` frame = `20.8%`; `11/77` on the wire (with FCS 4, preamble 8, IFG 12) = `14.3%`.

Note: offload off on `dev01` and `srv01` only; `rtr` has no `ethtool`.
