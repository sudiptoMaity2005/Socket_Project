# P2P Distributed Task Execution System

A **peer-to-peer distributed task execution system** written in pure C using Linux sockets. Any node on a LAN can offload a shell command or `.c` source file to the **least-loaded peer**, which compiles/executes it and **streams output back in real-time**.

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

### Key Components

| Module | Description |
|---|---|
| `discovery.c` | UDP broadcast HELLO/BYE + stale peer reaper |
| `peer_list.c` | Thread-safe peer registry (rwlock) |
| `load_monitor.c` | Reads `/proc/loadavg` + atomic active-task counter |
| `worker.c` | TCP server: fork+exec tasks, stream output |
| `dispatcher.c` | Load-queries peers, picks best, sends task via TCP |
| `main.c` | Arg parsing, subsystem startup, interactive REPL |

### Wire Protocol

All messages: **8-byte header** (type, flags, payload_len, reserved) + variable payload.

| Message | Transport | Purpose |
|---|---|---|
| `MSG_HELLO` | UDP broadcast | Announce presence every 5s |
| `MSG_BYE` | UDP broadcast | Graceful departure |
| `MSG_LOAD_REQ` | UDP unicast | Request CPU load |
| `MSG_LOAD_RESP` | UDP unicast | Respond with load1/5/15 + active tasks |
| `MSG_TASK_CMD` | TCP | Send shell command |
| `MSG_TASK_FILE` | TCP | Send .c source file |
| `MSG_OUTPUT` | TCP | stdout/stderr chunk |
| `MSG_TASK_DONE` | TCP | Exit code |

---

## Build

```bash
# Prerequisites: gcc, make, Linux (for /proc/loadavg)
cd "New socket proj"

# Release build
make

# Debug build with AddressSanitizer + UBSan
make debug
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

```
p2p> run <shell command>    # dispatch to least-loaded peer
p2p> submit <file.c>        # compile & run on least-loaded peer
p2p> peers                  # show discovered peers + loads
p2p> quit                   # broadcast BYE and exit
```

---

## Examples

```bash
# On Node A (Terminal 1)
./p2p_node --port 7777

# On Node B (Terminal 2)
./p2p_node --port 7778

# Wait ~5s for discovery, then in Node A's REPL:
p2p> peers
# Shows Node B in the list

p2p> run "uname -a"
# Executes on least-loaded node, output streamed back

p2p> submit /tmp/hello.c
# hello.c compiled & run on least-loaded node, output streamed back
```

### Sample hello.c

```c
#include <stdio.h>
int main() {
    printf("Hello from the P2P network!\n");
    return 0;
}
```

---

## Load Balancing

Each node continuously reads `/proc/loadavg` and tracks active task count.

**Scoring formula**: `score = load1 + 0.5 × active_tasks`

When dispatching:
1. All known peers are queried via UDP `LOAD_REQ` (500ms timeout)
2. Scores are computed for all responding peers + self
3. Task is routed to the node with the **lowest score**
4. If no peers respond → **falls back to local execution**

---

## Notes

- Nodes discover each other automatically via UDP broadcast on port `9999`
- Peers not seen for **30 seconds** are automatically evicted
- Maximum **8 concurrent tasks** per node (configurable in `common.h`)
- Maximum `.c` file size: **4 MB**
- No authentication — designed for **trusted LAN environments**
