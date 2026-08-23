# TH07 Relay Probe Service

This is the UDP relay service used by the TH07 iOS Online launcher. It matches
the launcher-side relay endpoint and room code fields.

Current scope:

- Accept a UDP probe packet from the launcher
- Reply with `THR1 PROBE_ACK <nonce>`
- Also send an extra `THR1 META <nonce> <timestamp_ms> <sha256>` packet
- Accept room registration from host/guest
- Forward raw gameplay UDP payloads between the two peers in the same room
- For each relayed gameplay datagram, also send a `THR1 TRACE <timestamp_ms> <sha256>` sideband packet
- The launcher uses `PROBE_ACK` to determine reachability and RTT, and discards the extra `META` / `TRACE` packets
- Service logs now include registration/leave events, room-ready events, dropped gameplay packets, and gameplay traffic diagnostics

Protocol:

- Client -> server:
  - `THR1 PROBE <nonce> <protocol_version>`
  - `THR1 REGISTER <room> <host|guest> <protocol_version> <session_id>`
- Server -> client:
  - `THR1 PROBE_ACK <nonce>`
  - `THR1 META <nonce> <timestamp_ms> <sha256>`
  - `THR1 TRACE <timestamp_ms> <sha256>`
  - `THR1 REGISTERED <room> <role>`
  - `THR1 REGISTER_FAILED room_occupied <room>`
  - `THR1 WAIT <room>`
  - `THR1 READY <room> <role> <peer_ip> <peer_port>`

Room locking:

- Once a room has successfully paired both `host` and `guest`, the relay locks that room to the same two launcher sessions
- If one side temporarily disconnects, only that same side may reclaim the slot; a third launcher will receive `REGISTER_FAILED room_occupied <room>`

Gameplay forwarding:

- After both peers in the same room are ready, the server forwards raw binary gameplay datagrams between them
- For every forwarded gameplay datagram, the server also emits a `TRACE` sideband packet carrying the server timestamp and SHA-256 of the forwarded payload

Install on Linux:

```bash
cd /path/to/th06/tools/relay_service
sudo ./install.sh 3478
```

After installation, make sure your VPS panel exposes the same UDP port externally.

Deploy from Windows/macOS/Linux over SSH:

```bash
python upload.py --host 211.154.22.21 --ssh-port 49420
```

The script prompts for the SSH password, uploads `th06_relay_server.py`, `install.sh`, and `README.md`,
then runs `install.sh` remotely and restarts `th06-relay`.

Traffic diagnostics:

- Default install uses `summary` mode
- In `summary` mode, the service logs one traffic summary per room and direction approximately once per second
- `packet` mode logs every forwarded gameplay packet and is much noisier

Examples:

```bash
journalctl -u th06-relay -f -n 50 --no-pager
```

Reinstall with packet-level logs:

```bash
export TH06_RELAY_TRAFFIC_MODE=packet
./install.sh 3478
```
