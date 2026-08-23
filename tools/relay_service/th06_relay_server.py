#!/usr/bin/env python3
import argparse
import hashlib
import logging
import signal
import socket
import struct
import sys
import time
from dataclasses import dataclass
from typing import Dict, Optional, Tuple


LOG = logging.getLogger("th06-relay")
RUNNING = True
STALE_TIMEOUT_S = 120.0


PeerAddr = Tuple[object, ...]


@dataclass
class PeerRegistration:
    room: str
    role: str
    proto: int
    addr: PeerAddr
    session_id: str
    last_seen: float


@dataclass
class TrafficCounter:
    total_packets: int = 0
    total_bytes: int = 0
    interval_packets: int = 0
    interval_bytes: int = 0
    last_log_time: float = 0.0
    last_payload_size: int = 0
    last_payload_sha: str = ""
    last_packet_type: int = -1
    last_packet_seq: int = -1
    last_ctrl_frame: int = -1
    last_ctrl_type: int = -1
    src_text: str = ""
    dst_text: str = ""


def handle_signal(signum, frame):
    del signum, frame
    global RUNNING
    RUNNING = False


def build_probe_ack(nonce: str) -> bytes:
    return f"THR1 PROBE_ACK {nonce}".encode("ascii")


def build_meta(nonce: str, payload: bytes) -> bytes:
    timestamp_ms = int(time.time() * 1000)
    digest = hashlib.sha256(payload).hexdigest()
    return f"THR1 META {nonce} {timestamp_ms} {digest}".encode("ascii")


def build_trace(payload: bytes) -> bytes:
    timestamp_ms = int(time.time() * 1000)
    digest = hashlib.sha256(payload).hexdigest()
    return f"THR1 TRACE {timestamp_ms} {digest}".encode("ascii")


def addr_ip(addr: PeerAddr) -> str:
    return str(addr[0])


def addr_port(addr: PeerAddr) -> int:
    return int(addr[1])


def addr_text(addr: PeerAddr) -> str:
    return f"{addr_ip(addr)}:{addr_port(addr)}"


def send_text(sock: socket.socket, addr: PeerAddr, text: str):
    sock.sendto(text.encode("ascii"), addr)


def send_meta(sock: socket.socket, addr: PeerAddr, nonce: str, payload: bytes):
    sock.sendto(build_meta(nonce, payload), addr)


def send_trace(sock: socket.socket, addr: PeerAddr, payload: bytes):
    sock.sendto(build_trace(payload), addr)


def payload_sha_prefix(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()[:12]


def parse_packet_header(payload: bytes) -> Tuple[int, int, int, int]:
    if len(payload) < 32:
        return -1, -1, -1, -1

    try:
        pkt_type, seq, _send_tick, _echo_tick, ctrl_frame, ctrl_type = struct.unpack_from("<IIQQii", payload, 0)
        return pkt_type, seq, ctrl_frame, ctrl_type
    except struct.error:
        return -1, -1, -1, -1


def cleanup_stale(
    rooms: Dict[str, Dict[str, PeerRegistration]],
    peers_by_addr: Dict[PeerAddr, PeerRegistration],
    room_locks: Dict[str, Dict[str, str]],
):
    now = time.time()
    expired = [addr for addr, reg in peers_by_addr.items() if now - reg.last_seen > STALE_TIMEOUT_S]
    for addr in expired:
        reg = peers_by_addr.pop(addr, None)
        if reg is None:
            continue
        LOG.info("stale peer removed room=%s role=%s addr=%s", reg.room, reg.role, addr_text(addr))
        room = rooms.get(reg.room)
        if room and room.get(reg.role) and room[reg.role].addr == addr:
            del room[reg.role]
            if not room:
                del rooms[reg.room]
                room_locks.pop(reg.room, None)


def remove_registration(
    rooms: Dict[str, Dict[str, PeerRegistration]],
    peers_by_addr: Dict[PeerAddr, PeerRegistration],
    room_locks: Dict[str, Dict[str, str]],
    registration: PeerRegistration,
):
    peers_by_addr.pop(registration.addr, None)
    room = rooms.get(registration.room)
    if room and room.get(registration.role) and room[registration.role].addr == registration.addr:
        del room[registration.role]
        if not room:
            del rooms[registration.room]
            room_locks.pop(registration.room, None)


def replace_registration(
    rooms: Dict[str, Dict[str, PeerRegistration]],
    peers_by_addr: Dict[PeerAddr, PeerRegistration],
    room_locks: Dict[str, Dict[str, str]],
    registration: PeerRegistration,
):
    old = peers_by_addr.get(registration.addr)
    if old is not None:
        remove_registration(rooms, peers_by_addr, room_locks, old)

    room = rooms.setdefault(registration.room, {})
    old_role_peer = room.get(registration.role)
    if old_role_peer is not None and old_role_peer.addr != registration.addr:
        remove_registration(rooms, peers_by_addr, room_locks, old_role_peer)
        room = rooms.setdefault(registration.room, {})
    room[registration.role] = registration
    peers_by_addr[registration.addr] = registration
    LOG.info("registered room=%s role=%s proto=%d addr=%s", registration.room, registration.role,
             registration.proto, addr_text(registration.addr))


def is_room_occupied(
    rooms: Dict[str, Dict[str, PeerRegistration]],
    room_locks: Dict[str, Dict[str, str]],
    registration: PeerRegistration,
) -> bool:
    locked_room = room_locks.get(registration.room)
    if locked_room is not None:
        return locked_room.get(registration.role) != registration.session_id

    room = rooms.get(registration.room)
    if room is None or "host" not in room or "guest" not in room:
        return False

    current_peer = room.get(registration.role)
    return current_peer is None or current_peer.addr != registration.addr


def log_room_ready(room: str, host: PeerRegistration, guest: PeerRegistration):
    LOG.info("room ready room=%s host=%s guest=%s proto=%d", room, addr_text(host.addr), addr_text(guest.addr), host.proto)


def log_traffic(
    traffic_counters: Dict[Tuple[str, str], TrafficCounter],
    room: str,
    direction: str,
    src_addr: PeerAddr,
    dst_addr: PeerAddr,
    payload: bytes,
    traffic_log_mode: str,
    traffic_summary_interval: float,
):
    if traffic_log_mode == "off":
        return

    now = time.time()
    key = (room, direction)
    counter = traffic_counters.setdefault(key, TrafficCounter(last_log_time=now))
    counter.total_packets += 1
    counter.total_bytes += len(payload)
    counter.interval_packets += 1
    counter.interval_bytes += len(payload)
    counter.last_payload_size = len(payload)
    counter.last_payload_sha = payload_sha_prefix(payload)
    counter.last_packet_type, counter.last_packet_seq, counter.last_ctrl_frame, counter.last_ctrl_type = parse_packet_header(payload)
    counter.src_text = addr_text(src_addr)
    counter.dst_text = addr_text(dst_addr)

    if traffic_log_mode == "packet":
        LOG.info(
            "forward room=%s dir=%s bytes=%d sha=%s type=%d seq=%d frame=%d ctrl=%d src=%s dst=%s total_packets=%d total_bytes=%d",
            room,
            direction,
            counter.last_payload_size,
            counter.last_payload_sha,
            counter.last_packet_type,
            counter.last_packet_seq,
            counter.last_ctrl_frame,
            counter.last_ctrl_type,
            counter.src_text,
            counter.dst_text,
            counter.total_packets,
            counter.total_bytes,
        )
        return

    if now - counter.last_log_time < traffic_summary_interval:
        return

    LOG.info(
        "traffic room=%s dir=%s interval_packets=%d interval_bytes=%d last_bytes=%d last_sha=%s type=%d seq=%d frame=%d ctrl=%d src=%s dst=%s total_packets=%d total_bytes=%d",
        room,
        direction,
        counter.interval_packets,
        counter.interval_bytes,
        counter.last_payload_size,
        counter.last_payload_sha,
        counter.last_packet_type,
        counter.last_packet_seq,
        counter.last_ctrl_frame,
        counter.last_ctrl_type,
        counter.src_text,
        counter.dst_text,
        counter.total_packets,
        counter.total_bytes,
    )
    counter.interval_packets = 0
    counter.interval_bytes = 0
    counter.last_log_time = now


def send_room_state(
    sock: socket.socket,
    registration: PeerRegistration,
    rooms: Dict[str, Dict[str, PeerRegistration]],
    room_locks: Dict[str, Dict[str, str]],
    nonce: str,
    payload: bytes,
):
    room = rooms.get(registration.room, {})
    other_role = "guest" if registration.role == "host" else "host"
    other = room.get(other_role)

    send_text(sock, registration.addr, f"THR1 REGISTERED {registration.room} {registration.role}")
    send_meta(sock, registration.addr, nonce, payload)

    if other is None:
        send_text(sock, registration.addr, f"THR1 WAIT {registration.room}")
        return

    if other.proto != registration.proto:
        LOG.info("room version mismatch room=%s local_role=%s local_proto=%d other_proto=%d",
                 registration.room, registration.role, registration.proto, other.proto)
        send_text(sock, registration.addr, f"THR1 VERSION_MISMATCH {other.proto}")
        send_text(sock, other.addr, f"THR1 VERSION_MISMATCH {registration.proto}")
        return

    room_locks[registration.room] = {
        "host": room["host"].session_id,
        "guest": room["guest"].session_id,
    }

    send_text(
        sock,
        registration.addr,
        f"THR1 READY {registration.room} {registration.role} {addr_ip(other.addr)} {addr_port(other.addr)}",
    )
    send_text(
        sock,
        other.addr,
        f"THR1 READY {other.room} {other.role} {addr_ip(registration.addr)} {addr_port(registration.addr)}",
    )
    if registration.role == "host":
        log_room_ready(registration.room, registration, other)
    else:
        log_room_ready(registration.room, other, registration)


def handle_control(
    sock: socket.socket,
    payload: bytes,
    text: str,
    addr: PeerAddr,
    rooms: Dict[str, Dict[str, PeerRegistration]],
    peers_by_addr: Dict[PeerAddr, PeerRegistration],
    room_locks: Dict[str, Dict[str, str]],
):
    tokens = text.split()
    if len(tokens) >= 4 and tokens[0] == "THR1" and tokens[1] == "PROBE":
        nonce = tokens[2]
        sock.sendto(build_probe_ack(nonce), addr)
        send_meta(sock, addr, nonce, payload)
        return

    if len(tokens) >= 5 and tokens[0] == "THR1" and tokens[1] == "REGISTER":
        room = tokens[2]
        role = tokens[3].lower()
        nonce = tokens[2]
        try:
            proto = int(tokens[4])
        except ValueError:
            send_text(sock, addr, "THR1 REGISTER_FAILED bad_proto")
            return

        if role not in {"host", "guest"}:
            send_text(sock, addr, "THR1 REGISTER_FAILED bad_role")
            return

        session_id = tokens[5] if len(tokens) >= 6 else addr_text(addr)
        registration = PeerRegistration(room=room, role=role, proto=proto, addr=addr, session_id=session_id,
                                        last_seen=time.time())
        if is_room_occupied(rooms, room_locks, registration):
            LOG.info("room occupied room=%s role=%s addr=%s", registration.room, registration.role, addr_text(addr))
            send_text(sock, addr, f"THR1 REGISTER_FAILED room_occupied {registration.room}")
            return
        replace_registration(rooms, peers_by_addr, room_locks, registration)
        send_room_state(sock, registration, rooms, room_locks, room, payload)
        return

    if len(tokens) >= 3 and tokens[0] == "THR1" and tokens[1] == "LEAVE":
        reg = peers_by_addr.pop(addr, None)
        if reg is None:
            return
        LOG.info("peer left room=%s role=%s addr=%s", reg.room, reg.role, addr_text(addr))
        room = rooms.get(reg.room)
        if room and room.get(reg.role) and room[reg.role].addr == addr:
            del room[reg.role]
            if not room:
                del rooms[reg.room]
                room_locks.pop(reg.room, None)
        return


def handle_gameplay(
    sock: socket.socket,
    payload: bytes,
    addr: PeerAddr,
    rooms: Dict[str, Dict[str, PeerRegistration]],
    peers_by_addr: Dict[PeerAddr, PeerRegistration],
    traffic_counters: Dict[Tuple[str, str], TrafficCounter],
    traffic_log_mode: str,
    traffic_summary_interval: float,
):
    registration = peers_by_addr.get(addr)
    if registration is None:
        LOG.debug("drop gameplay bytes=%d sha=%s addr=%s reason=unregistered",
                  len(payload), payload_sha_prefix(payload), addr_text(addr))
        return

    registration.last_seen = time.time()
    room = rooms.get(registration.room, {})
    current_peer = room.get(registration.role)
    if current_peer is None or current_peer.addr != addr:
        LOG.debug("drop gameplay room=%s role=%s bytes=%d sha=%s reason=stale_registration",
                  registration.room, registration.role, len(payload), payload_sha_prefix(payload))
        return

    other_role = "guest" if registration.role == "host" else "host"
    other = room.get(other_role)
    if other is None:
        LOG.debug("drop gameplay room=%s role=%s bytes=%d sha=%s reason=missing_peer",
                  registration.room, registration.role, len(payload), payload_sha_prefix(payload))
        return

    other.last_seen = time.time()
    sock.sendto(payload, other.addr)
    direction = "host->guest" if registration.role == "host" else "guest->host"
    log_traffic(traffic_counters, registration.room, direction, registration.addr, other.addr, payload,
                traffic_log_mode, traffic_summary_interval)
    # Sideband diagnostics for relayed gameplay packets; clients ignore this payload.
    send_trace(sock, addr, payload)
    send_trace(sock, other.addr, payload)


def serve(host: str, port: int, traffic_log_mode: str, traffic_summary_interval: float) -> int:
    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    sock.bind((host, port))
    sock.settimeout(0.5)

    rooms: Dict[str, Dict[str, PeerRegistration]] = {}
    peers_by_addr: Dict[PeerAddr, PeerRegistration] = {}
    room_locks: Dict[str, Dict[str, str]] = {}
    traffic_counters: Dict[Tuple[str, str], TrafficCounter] = {}

    LOG.info("relay listening on [%s]:%d/udp traffic_log_mode=%s summary_interval=%.2fs",
             host, port, traffic_log_mode, traffic_summary_interval)

    while RUNNING:
        cleanup_stale(rooms, peers_by_addr, room_locks)

        try:
            payload, addr = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError as exc:
            LOG.exception("recvfrom failed: %s", exc)
            return 1

        try:
            text = payload.decode("ascii", errors="ignore").strip()
        except Exception:
            text = ""

        if text.startswith("THR1 "):
            LOG.info("control from %s: %r", addr, text)
            handle_control(sock, payload, text, addr, rooms, peers_by_addr, room_locks)
        else:
            handle_gameplay(sock, payload, addr, rooms, peers_by_addr, traffic_counters, traffic_log_mode,
                            traffic_summary_interval)

    LOG.info("relay shutting down")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH06 UDP relay and room service")
    parser.add_argument("--host", default="::", help="Bind host, defaults to ::")
    parser.add_argument("--port", type=int, default=3478, help="UDP port, defaults to 3478")
    parser.add_argument("--log-level", default="INFO", help="Python log level")
    parser.add_argument("--traffic-log-mode", choices=("summary", "packet", "off"), default="summary",
                        help="Gameplay traffic logging mode")
    parser.add_argument("--traffic-summary-interval", type=float, default=1.0,
                        help="Summary logging interval in seconds when traffic log mode is summary")
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(message)s",
    )

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        return serve(args.host, args.port, args.traffic_log_mode, args.traffic_summary_interval)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
