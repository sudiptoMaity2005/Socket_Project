#!/usr/bin/env python3
"""
P2P Node Web Bridge Server
──────────────────────────
Serves the web UI (index.html) via HTTP and connects to the running
p2p_node process via WebSocket so the browser can:
  • Send REPL commands (run, submit, peers, quit)
  • Receive real-time output streamed back

Usage:
  python3 server.py [--p2p-port 7777] [--web-port 8080]

The script spawns `../p2p_node` automatically.
"""

import argparse
import asyncio
import json
import os
import pathlib
import signal
import subprocess
import sys
import threading

# ── Try to import websockets; guide user if missing ─────────────────────────
try:
    import websockets
    from websockets.server import serve as ws_serve
except ImportError:
    print("Missing dependency: websockets")
    print("Install with:  pip install websockets")
    sys.exit(1)

from http.server import HTTPServer, SimpleHTTPRequestHandler
from functools import partial

# ── Globals ──────────────────────────────────────────────────────────────────
BASE_DIR   = pathlib.Path(__file__).parent          # web/
BINARY     = BASE_DIR.parent / "p2p_node"
OUTPUT_LOG: list[str] = []
p2p_proc = None  # type: asyncio.subprocess.Process
connected_clients = set()


# ── HTTP server (serves web/ directory) ─────────────────────────────────────
class SilentHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(BASE_DIR), **kwargs)

    def log_message(self, fmt, *args):  # silence access logs
        pass


def run_http(port: int):
    httpd = HTTPServer(("0.0.0.0", port), SilentHandler)
    print(f"[web]  HTTP  → http://localhost:{port}")
    httpd.serve_forever()


# ── Broadcast a JSON message to all WS clients ───────────────────────────────
async def broadcast(msg: dict):
    if not connected_clients:
        return
    data = json.dumps(msg)
    await asyncio.gather(*(c.send(data) for c in list(connected_clients)),
                         return_exceptions=True)


# ── Read p2p_node stdout/stderr and forward to WS clients ────────────────────
async def pipe_output(stream, kind: str):
    while True:
        # Use read(n) instead of readline() to avoid blocking on prompts without newlines
        raw = await stream.read(1024)
        if not raw:
            break
        text = raw.decode(errors="replace")
        OUTPUT_LOG.append(text)
        await broadcast({"type": "output", "kind": kind, "line": text})
    await broadcast({"type": "done", "stream": kind})


# ── WebSocket handler ─────────────────────────────────────────────────────────
async def ws_handler(websocket):
    connected_clients.add(websocket)
    # Send backlog of previously captured output
    for line in OUTPUT_LOG[-200:]:
        await websocket.send(json.dumps({"type": "output", "kind": "stdout", "line": line}))

    try:
        async for raw in websocket:
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            if msg.get("type") == "command":
                cmd = str(msg.get("cmd", "")).strip()
                if not cmd:
                    continue
                if p2p_proc and p2p_proc.stdin:
                    try:
                        p2p_proc.stdin.write((cmd + "\n").encode())
                        await p2p_proc.stdin.drain()
                    except Exception as exc:
                        await websocket.send(json.dumps(
                            {"type": "error", "line": f"Send error: {exc}"}))

            elif msg.get("type") == "file_upload":
                filename = msg.get("filename", "upload.c")
                content = msg.get("content", "")
                # Save to a temporary-ish path in the project root
                path = BINARY.parent / f"web_upload_{filename}"
                try:
                    with open(path, "w") as f:
                        f.write(content)
                    if p2p_proc and p2p_proc.stdin:
                        p2p_proc.stdin.write(f"submit {path}\n".encode())
                        await p2p_proc.stdin.drain()
                except Exception as exc:
                    await websocket.send(json.dumps(
                        {"type": "error", "line": f"Upload error: {exc}"}))

            elif msg.get("type") == "status":
                alive = p2p_proc is not None and p2p_proc.returncode is None
                await websocket.send(json.dumps({"type": "status", "alive": alive}))

    except websockets.exceptions.ConnectionClosedError:
        pass
    finally:
        connected_clients.discard(websocket)


# ── Spawn the p2p_node binary ────────────────────────────────────────────────
async def spawn_p2p(p2p_port: int, iface, loop):
    global p2p_proc
    if not BINARY.exists():
        print(f"[error] Binary not found: {BINARY}")
        print("        Run `make` in the project root first.")
        return

    cmd = [str(BINARY), "--port", str(p2p_port)]
    if iface:
        cmd += ["--iface", iface]

    p2p_proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )
    print(f"[p2p]  Spawned p2p_node PID={p2p_proc.pid}  port={p2p_port}")

    asyncio.ensure_future(pipe_output(p2p_proc.stdout, "stdout"))
    await p2p_proc.wait()
    print("[p2p]  p2p_node exited")
    await broadcast({"type": "exited"})


# ── Main ──────────────────────────────────────────────────────────────────────
async def main_async(args):
    # HTTP in a background thread
    http_thread = threading.Thread(
        target=run_http, args=(args.web_port,), daemon=True)
    http_thread.start()

    # WebSocket server
    print(f"[web]  WS    → ws://localhost:{args.ws_port}")
    ws_server = await ws_serve(ws_handler, "0.0.0.0", args.ws_port)

    # Spawn binary
    loop = asyncio.get_event_loop()
    asyncio.ensure_future(spawn_p2p(args.p2p_port, args.iface, loop))

    await ws_server.wait_closed()


def main():
    parser = argparse.ArgumentParser(description="P2P Node Web Bridge")
    parser.add_argument("--p2p-port", type=int, default=7777,
                        help="TCP port for the p2p_node worker (default 7777)")
    parser.add_argument("--web-port", type=int, default=8080,
                        help="HTTP port for the web UI (default 8080)")
    parser.add_argument("--ws-port",  type=int, default=8765,
                        help="WebSocket port (default 8765)")
    parser.add_argument("--iface",    type=str, default=None,
                        help="Network interface for p2p_node (optional)")
    args = parser.parse_args()

    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        print("\n[web] Shutting down.")
        if p2p_proc and p2p_proc.returncode is None:
            p2p_proc.terminate()


if __name__ == "__main__":
    main()
