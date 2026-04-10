# P2P Distributed Task Execution System

> A **peer-to-peer distributed task execution system** written in pure C using Linux sockets.  
> Any node on a LAN can offload a shell command or `.c` source file to the **least-loaded peer**, which compiles/executes it and **streams output back in real-time**.

![Language](https://img.shields.io/badge/language-C-blue)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

---

## Table of Contents

- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Wire Protocol](#wire-protocol)
- [Build](#build)
- [Usage](#usage)
- [Examples](#examples)
- [Load Balancing](#load-balancing)
- [Notes](#notes)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    LAN (UDP broadcast domain)               │
│                                                             │
│  ┌──────────────────┐     HELLO/BYE (UDP bcast)            │
│  │   Node A          │◄────────────────────────────────────┐│
│  │  ┌────────────┐  │                                      ││
│  │  │ CLI / REPL │  │  LOAD_REQ (UDP unicast)              ││
│  │  └─────┬──────┘  │──────────────────────────────►┌──────┴┤
│  │        │         │                                │Node B │
│  │  ┌─────▼──────┐  │  TCP: TASK + stream output    │Worker │
│  │  │ Dispatcher │──┼──────────────────────────────►│Thread │
│  │  └────────────┘  │◄── MSG_OUTPUT chunks ─────────┤       │
│  │                  │◄── MSG_TASK_DONE ──────────────┤       │
│  └──────────────────┘                                └───────┘
└─────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
New_socket_proj/
├── main.c            # Entry point: argument parsing, subsystem startup, REPL
├── common.h          # Shared constants, wire protocol structs, macros
├── net_utils.c/h     # Socket helpers (bind, connect, read_exact, etc.)
├── peer_list.c/h     # Thread-safe peer registry (rwlock-protected)
├── discovery.c/h     # UDP broadcast HELLO/BYE + stale peer reaper thread
├── load_monitor.c/h  # Reads /proc/loadavg + atomic active-task counter
├── worker.c/h        # TCP server: fork+exec tasks, stream stdout/stderr back
├── dispatcher.c/h    # Load-queries peers, picks best, sends task via TCP
└── Makefile
```

---

## Wire Protocol

All messages use an **8-byte header** (`type`, `flags`, `payload_len`, `reserved`) followed by a variable-length payload.

| Message        | Transport     | Purpose                                         |
|----------------|---------------|-------------------------------------------------|
| `MSG_HELLO`    | UDP broadcast | Announce presence every 5 s                     |
| `MSG_BYE`      | UDP broadcast | Graceful departure                              |
| `MSG_LOAD_REQ` | UDP unicast   | Request CPU load from a peer                    |
| `MSG_LOAD_RESP`| UDP unicast   | Reply with load1/5/15 + active task count       |
| `MSG_TASK_CMD` | TCP           | Send a shell command to execute                 |
| `MSG_TASK_FILE`| TCP           | Send a `.c` source file to compile and run      |
| `MSG_OUTPUT`   | TCP           | stdout/stderr chunk streamed back               |
| `MSG_TASK_DONE`| TCP           | Task finished — carries exit code               |

---

## Build

**Prerequisites:** `gcc`, `make`, Linux kernel (requires `/proc/loadavg`)

```bash
# Clone / enter the directory
cd New_socket_proj

# Release build
make

# Debug build (AddressSanitizer + UBSan)
make debug

# Clean build artifacts
make clean
```

---

## Usage

```bash
# Start a node (auto-detects local IP)
./p2p_node

# Specify port and network interface
./p2p_node --port 7777 --iface eth0
```

### REPL Commands

| Command                 | Description                                |
|-------------------------|--------------------------------------------|
| `run <shell command>`   | Dispatch command to the least-loaded peer  |
| `submit <file.c>`       | Compile & run `.c` file on least-loaded peer|
| `peers`                 | Show discovered peers and their load info  |
| `quit`                  | Broadcast BYE and exit                     |

---

## Examples

```bash
# Terminal 1 — Node A
./p2p_node --port 7777

# Terminal 2 — Node B
./p2p_node --port 7778
```

Wait ~5 seconds for peer discovery, then use Node A's REPL:

```
p2p> peers
# Lists Node B with its current load

p2p> run "uname -a"
# Executes on the least-loaded node; output streamed back live

p2p> submit /tmp/hello.c
# hello.c is compiled & run on the best peer; output streamed back
```

### Sample `hello.c`

```c
#include <stdio.h>
int main() {
    printf("Hello from the P2P network!\n");
    return 0;
}
```

---

## Load Balancing

Each node continuously reads `/proc/loadavg` and tracks its active task count.

**Scoring formula:**

```
score = load1 + 0.5 × active_tasks
```

**Dispatch algorithm:**
1. All known peers are queried via UDP `LOAD_REQ` (500 ms timeout).
2. Scores are computed for all responding peers **and** self.
3. Task is routed to the node with the **lowest score**.
4. If no peers respond → **falls back to local execution**.

---

## Notes

| Setting                  | Value / Default                      |
|--------------------------|--------------------------------------|
| Discovery transport      | UDP broadcast, port `9999`           |
| Worker transport         | TCP, default port `7777`             |
| Peer timeout             | 30 seconds since last HELLO          |
| Max concurrent tasks     | 8 per node (configurable in `common.h`) |
| Max `.c` file size       | 4 MB                                 |
| Security model           | No authentication — **trusted LAN only** |

> [!WARNING]
> This system has **no authentication or encryption**. Run it only on trusted local networks.

---

## File Reference (what each file does)

- `main.c`: Program entrypoint. Parses CLI options, initializes subsystems (`discovery`, `peer_list`, `load_monitor`, `worker`, `dispatcher`), and exposes the REPL used to submit tasks or query peers.
- `common.h`: Central shared definitions: limits, port defaults, wire protocol message types and header, `peer_info_t` and `task_t` structs, logging macros, and compile-time constants.
- `net_utils.c` / `net_utils.h`: Low-level socket helpers and utilities:
    - Create UDP broadcast sockets and TCP server sockets (`make_udp_broadcast_socket`, `make_tcp_server`).
    - Reliable send/receive helpers for framed messages (`send_all`, `recv_all`, `send_msg`, `recv_msg`).
    - Local IP detection (`get_local_ip`) and TCP connect wrapper (`tcp_connect`).
- `peer_list.c` / `peer_list.h`: Thread-safe in-memory peer registry. Stores `peer_info_t` entries, supports add/remove/touch, and reaping of stale peers.
- `discovery.c` / `discovery.h`: Peer discovery subsystem using UDP broadcasts:
    - Sends periodic `MSG_HELLO` broadcasts and optional `MSG_BYE` on shutdown.
    - Listens for HELLO/BYE from other nodes and updates the `peer_list`.
    - Runs a reaper thread to remove peers that haven't been seen for `PEER_TIMEOUT_SECS`.
- `load_monitor.c` / `load_monitor.h`: Tracks local load metrics:
    - Reads `/proc/loadavg` for load1/load5/load15.
    - Maintains an atomic counter of currently active tasks (`load_task_start`/`load_task_done`).
    - Exposes `load_get_score()` used by the dispatcher when choosing a node.
- `worker.c` / `worker.h`: TCP worker server that accepts `MSG_TASK_CMD` and `MSG_TASK_FILE` requests:
    - For commands: executes via `/bin/sh -c`, streams combined stdout/stderr back as `MSG_OUTPUT`, then sends `MSG_TASK_DONE`.
    - For files: writes source to `/tmp`, runs `gcc` to compile streaming compiler output, runs the binary if compile succeeds, then streams runtime output.
    - Enforces `MAX_CONCURRENT_TASKS` and spawns each connection handler in a detached thread.
- `dispatcher.c` / `dispatcher.h`: Responsible for routing tasks to the best peer:
    - Queries peers with `MSG_LOAD_REQ` (UDP unicast) and collects `MSG_LOAD_RESP`.
    - Computes a score (load + active tasks factor) and forwards the task via TCP to the chosen node, or runs locally if no peers respond.
- `worker.o`, `*.o`: Object files produced by `make`; remove with `make clean` when rebuilding.
- `Makefile`: Build rules. Targets: `all` (release), `debug` (ASan/UBSan), and `clean`. Uses `gcc` and standard `-Wall -Wextra` flags.

## web/ folder

The `web/` directory contains a tiny web UI used for testing or demonstration purposes:

- `web/app.js`: Minimal client-side JS for interacting with a backend (if included).
- `web/index.html`: Demo page / UI shell.
- `web/server.py`: Optional Python development server used to host the web page or provide lightweight HTTP endpoints for demoing the P2P node. Not required to run the C node.
- `web/style.css`: Basic styling for the demo page.

Notes: the web UI is separate from the P2P node core; it may interact with the node via custom endpoints if you add a small HTTP bridge.

---

If you'd like, I can:
- add short code pointers (functions) with line numbers for each important routine, or
- generate a quick `USAGE.md` with examples for testing multi-node behavior locally using different ports.

