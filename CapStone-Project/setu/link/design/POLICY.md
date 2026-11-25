# SETU-LINK Policy (`POLICY.md`)

## 1. Intent (Not Implemented)

- **Guest isolation (intent):** Guest (`VLAN 50`) cannot reach `mgmt` (`VLAN 40`) or `instrumentation` (`VLAN 60`) by construction.
- **Instrumentation protection (intent):** `instrumentation` (`VLAN 60`) cannot be starved by bulk traffic by construction.

## 2. Zone Table

| Segment | VLAN ID (`ADDRESS-PLAN.md`) | Allowed to reach |
| :--- | ---: | :--- |
| `staff` | 10 | `TODO(week 8/11)` |
| `student` | 20 | `TODO(week 8/11)` |
| `voice` | 30 | `TODO(week 8/11)` |
| `mgmt` | 40 | `TODO(week 8/11)` |
| `guest` | 50 | Internet only (intent) |
| `instrumentation` | 60 | `TODO(week 8/11)` |
| `servers` | 70 | `TODO(week 8/11)` |
| `storage` | 80 | `TODO(week 8/11)` |

## 3. ACL Ordering Rule

- **First match wins:** Place specific `deny` rules before broad `permit` rules (Fault Bank L7).

## 4. Status

Every rule in this document is **NOT IMPLEMENTED** and **NOT TESTED**.
