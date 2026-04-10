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
