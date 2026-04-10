#ifndef COMMON_H
#define COMMON_H

#ifndef __linux__
#  warning "This project is optimized for Linux. /proc/loadavg is not available; using fallback load detection."
#endif

#include <stdint.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ─── Ports ─────────────────────────────────────────────────────────────── */
#define DISCOVERY_PORT      9999   /* UDP broadcast port for peer discovery  */
#define DEFAULT_WORKER_PORT 7777   /* TCP port for task execution            */

/* ─── Limits ─────────────────────────────────────────────────────────────── */
#define MAX_PEERS           64
#define MAX_HOSTNAME_LEN    64
#define MAX_CMD_LEN         4096
#define MAX_FILENAME_LEN    256
#define MAX_FILE_SIZE       (4 * 1024 * 1024)   /* 4 MB max .c file         */
#define OUTPUT_BUF_SIZE     4096
#define PEER_TIMEOUT_SECS   30
#define DISCOVERY_INTERVAL  5      /* seconds between HELLO broadcasts       */
#define LOAD_QUERY_TIMEOUT_MS 500  /* ms to wait for LOAD_RESP               */
#define MAX_CONCURRENT_TASKS 8

/* ─── Wire Protocol ──────────────────────────────────────────────────────── */

/* Message types (1 byte) */
typedef enum __attribute__((packed)) {
    MSG_HELLO     = 0x01,  /* UDP broadcast: node announcement               */
    MSG_BYE       = 0x02,  /* UDP broadcast: node leaving                    */
    MSG_LOAD_REQ  = 0x03,  /* UDP unicast: request CPU load info             */
    MSG_LOAD_RESP = 0x04,  /* UDP unicast: respond with CPU load info        */
    MSG_TASK_FILE = 0x06,  /* TCP: dispatch a .c source file for compile+run */

    MSG_OUTPUT    = 0x07,  /* TCP: stdout/stderr chunk from worker           */
    MSG_TASK_DONE = 0x08,  /* TCP: task finished, carries exit code          */
} msg_type_t;

/* Fixed 8-byte message header (network byte order) */
typedef struct __attribute__((packed)) {
    uint8_t  type;         /* msg_type_t                                     */
    uint8_t  flags;        /* reserved                                       */
    uint32_t payload_len;  /* payload length in bytes (big-endian)           */
    uint16_t reserved;
} msg_header_t;

/* ─── Payload Structs (all fields in network byte order) ─────────────────── */

typedef struct __attribute__((packed)) {
    uint16_t tcp_port;
    char     hostname[MAX_HOSTNAME_LEN];
} payload_hello_t;

typedef struct __attribute__((packed)) {
    float    load_pct;

    uint32_t active_tasks;
    uint32_t total_tasks;

    uint32_t num_cores;
} payload_load_resp_t;

typedef struct __attribute__((packed)) {
    int32_t exit_code;
} payload_task_done_t;

/* ─── Peer Info ──────────────────────────────────────────────────────────── */

typedef struct {
    char     ip[INET_ADDRSTRLEN];
    uint16_t port;
    float    load_pct;

    uint32_t active_tasks;
    uint32_t total_tasks;

    uint32_t num_cores;
    time_t   last_seen;
    int      valid;        /* 1 = slot occupied                              */
} peer_info_t;

/* ─── Task Descriptor ────────────────────────────────────────────────────── */

typedef enum {
    TASK_FILE,
} task_type_t;


typedef struct {
    task_type_t type;
    char        filename[MAX_FILENAME_LEN]; /* original filename for display */

    uint8_t    *file_data;               /* heap-allocated for TASK_FILE    */
    uint32_t    file_size;
} task_t;

/* ─── Utility Macros ─────────────────────────────────────────────────────── */

#define LOG(fmt, ...) \
    fprintf(stderr, "[p2p %s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#define DIE(fmt, ...) \
    do { LOG("FATAL: " fmt, ##__VA_ARGS__); exit(EXIT_FAILURE); } while(0)

#endif /* COMMON_H */
