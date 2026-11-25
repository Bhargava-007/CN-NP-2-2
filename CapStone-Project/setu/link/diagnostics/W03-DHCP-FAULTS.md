# Week 3 DHCP Fault Bank (`W03-DHCP-FAULTS`)

Lab: `w03-services` | Client: `dev01` on `10.20.32.0/23` | Relay: `rtr` | DHCP server: Kea 2.2.0 on `kea01` (`10.20.48.9`, started by hand after deploy).

## Fault 1: Missing relay (L3)

### Hypothesis
With the relay stopped, `DISCOVER` never leaves the client segment, so no lease is obtained.

### Test
`Stop-SetuDhcpRelay rtr`; flush `dev01` `eth1`; `Invoke-SetuDhcpLease dev01 eth1`.

### Evidence (MEASURED)
- `udhcpc` sent 4 `DISCOVER`s, got no lease, and `eth1` has no IPv4 address.
- Kea log tail was unchanged from the previous lease (no new `DISCOVER`).
- Pre-fault `ping dev01 -> 10.20.48.10` worked, so routing is fine.
- Note: `udhcpc` does not self-assign `169.254.x.x` as a real client does. Evidence that Kea saw nothing is the unchanged log tail only (weak); no wire capture was taken.

### Fix
`Start-SetuDhcpRelay rtr eth1 eth2 10.20.48.9`.

### Verify (MEASURED)
Lease `10.20.32.20/23` obtained; new `DHCP4_LEASE_ALLOC` at `07:16:01`.

## Fault 2: Wrong scope (L3/L7)

### Hypothesis
If the Kea subnet does not match the relay `giaddr` (`10.20.32.1`), Kea drops the `DISCOVER` and the client gets no lease.

### Test
Container-only copy of the config with subnet `10.20.34.0/23`, pool `10.20.34.20-10.20.35.250`, reservation `10.20.34.10`, and log level `DEBUG 50` (repo file untouched).

### Evidence (MEASURED)
- `DISCOVER` reached Kea from `10.20.48.1`.
- Log lines: `"No subnet matches relay address: 10.20.32.1"` and `DHCP4_PACKET_NAK_0001` failed to select a subnet; no lease.
- Distinguishes from Fault 1: the packet arrives and is rejected.
- **INFERRED:** at `INFO` level the drop would be silent (not run).
- **Failed attempts (side findings, MEASURED):**
  - (a) Changing only the subnet failed at load: pool `10.20.32.20-10.20.33.250` not inside `10.20.34.0/23`.
  - (b) Changing subnet and pool failed at load: reservation `10.20.32.10` not inside the subnet. Kea validates pool and reservation against the subnet at load time, so a scope change needs three edits.
  - Also: `docker top` shows host PIDs, `kill` needs in-container PIDs; a stale Kea held port 67 (`"Address already in use"`) and made one lease attempt invalid.

### Fix
Kill both stale Kea processes, start Kea with `/etc/kea/kea-dhcp4.conf`.

### Verify (MEASURED)
`DHCP4_STARTED`, lease `10.20.32.20/23`, `DHCP4_LEASE_ALLOC` at `07:21:46`.

## Fault 3: DHCP server down (L7)

### Hypothesis
With Kea stopped, the relay still forwards `DISCOVER` but nobody answers, so no lease is obtained.

### Test
Kill Kea (in-container PID), confirm zero `kea-dhcp4` processes, run the lease.

### Evidence (MEASURED)
- `0` Kea processes; `udhcpc` got no lease after `DISCOVER`s.
- **NOT CAPTURED:** wire capture on `rtr eth2` returned empty twice (first run timing, second run cause unknown, `tcpdump`/`tshark` errors were discarded), so there is no wire proof that the relayed `DISCOVER` left `rtr`.
- Relay health is established by Faults 1 and 2, not by this test.

### Fix
Start Kea.

### Verify (MEASURED)
`DHCP4_STARTED` at `07:26:37`, lease `10.20.32.20/23`, `DHCP4_LEASE_ALLOC` at `07:26:40`.

## Lab notes (MEASURED)
- Kit topology needed fixes before DHCP worked:
  - `apt` conffile prompt hung `kea01` (bind-mounted config).
  - Boot chain crash-looped and dropped `eth1` (`restarts=8`), so `kea01` idles on `sleep infinity`.
  - Kea 2.2.0 needs `output_options`, not `output-options`.
  - Switches needed `New-SetuBridge`.
  - `dev01` had a default route via `eth0` that won, so plain `ping` failed until it was deleted (`ping -I eth1` worked).
