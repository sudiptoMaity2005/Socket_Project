#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <arpa/inet.h>

#include "common.h"
#include "net_utils.h"
#include "peer_list.h"
#include "discovery.h"
#include "worker.h"
#include "dispatcher.h"
#include "load_monitor.h"

/* ─── Globals ────────────────────────────────────────────────────────────── */
static char     g_local_ip[INET_ADDRSTRLEN] = "127.0.0.1";
static uint16_t g_own_port = DEFAULT_WORKER_PORT;
static volatile int g_running = 1;

/* ─── SIGINT handler ─────────────────────────────────────────────────────── */
static void on_sigint(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[p2p] Shutting down...\n");
    fflush(stdout);
}

/*
 * UDP LOAD_REQ responder thread.
 * Listens on DISCOVERY_PORT for MSG_LOAD_REQ and replies with MSG_LOAD_RESP.
 * This runs independently from the discovery listener because they share
 * the same port via SO_REUSEPORT.
 */
static void *load_responder_thread(void *arg) {
    (void)arg;

    int fd = make_udp_broadcast_socket(DISCOVERY_PORT);
    if (fd < 0) {
        LOG("load_responder: cannot bind port %d", DISCOVERY_PORT);
        return NULL;
    }

    uint8_t buf[sizeof(msg_header_t) + 16];
    for (;;) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&src, &src_len);
        if (n < (ssize_t)sizeof(msg_header_t)) continue;

        msg_header_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));
        if (hdr.type != MSG_LOAD_REQ) continue;

        /* Build response */
        payload_load_resp_t resp;
        float l1 = 0, l5 = 0, l15 = 0;
        load_get(&l1, &l5, &l15);

        /* Store floats in network byte order */
        uint32_t tmp;
        memcpy(&tmp, &l1,  4); tmp = htonl(tmp); memcpy(&resp.load1,  &tmp, 4);
        memcpy(&tmp, &l5,  4); tmp = htonl(tmp); memcpy(&resp.load5,  &tmp, 4);
        memcpy(&tmp, &l15, 4); tmp = htonl(tmp); memcpy(&resp.load15, &tmp, 4);
        resp.active_tasks = htonl(load_get_active_tasks());

        /* Send response directly back to the querier's source port */
        msg_header_t resp_hdr;
        memset(&resp_hdr, 0, sizeof(resp_hdr));
        resp_hdr.type        = MSG_LOAD_RESP;
        resp_hdr.payload_len = htonl((uint32_t)sizeof(resp));

        uint8_t pkt[sizeof(resp_hdr) + sizeof(resp)];
        memcpy(pkt,                &resp_hdr, sizeof(resp_hdr));
        memcpy(pkt + sizeof(resp_hdr), &resp,     sizeof(resp));

        sendto(fd, pkt, sizeof(pkt), 0,
               (struct sockaddr *)&src, src_len);
    }
    return NULL;
}

/* ─── Read a .c file into heap ───────────────────────────────────────────── */
static uint8_t *read_file(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > MAX_FILE_SIZE) {
        fprintf(stderr, "File too large or empty: %s\n", path);
        fclose(f);
        return NULL;
    }
    uint8_t *data = malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)sz;
    return data;
}

/* ─── REPL ───────────────────────────────────────────────────────────────── */
static void run_repl(void) {
    char line[MAX_CMD_LEN + 32];
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  P2P Distributed Task Execution Shell  ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("Commands:\n");
    printf("  run <shell command>    — dispatch command to least-loaded peer\n");
    printf("  submit <file.c>        — compile & run .c file on least-loaded peer\n");
    printf("  peers                  — list discovered peers and their loads\n");
    printf("  quit                   — broadcast BYE and exit\n\n");

    while (g_running) {
        printf("p2p> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;

        /* ── peers ── */
        if (strcmp(line, "peers") == 0) {
            peer_list_print();
            continue;
        }

        /* ── quit ── */
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            g_running = 0;
            break;
        }

        /* ── run <cmd> ── */
        if (strncmp(line, "run ", 4) == 0) {
            const char *cmd = line + 4;
            while (*cmd == ' ') cmd++;
            if (*cmd == '\0') { printf("Usage: run <command>\n"); continue; }

            task_t t;
            memset(&t, 0, sizeof(t));
            t.type = TASK_CMD;
            strncpy(t.cmd, cmd, MAX_CMD_LEN - 1);

            printf("--- output ---\n");
            int rc = dispatch(&t, g_local_ip, g_own_port);
            printf("--- exit code: %d ---\n\n", rc);
            continue;
        }

        /* ── submit <file.c> ── */
        if (strncmp(line, "submit ", 7) == 0) {
            const char *path = line + 7;
            while (*path == ' ') path++;
            if (*path == '\0') { printf("Usage: submit <file.c>\n"); continue; }

            uint32_t fsz = 0;
            uint8_t *data = read_file(path, &fsz);
            if (!data) continue;

            /* Extract basename */
            const char *basename = strrchr(path, '/');
            basename = basename ? basename + 1 : path;

            task_t t;
            memset(&t, 0, sizeof(t));
            t.type      = TASK_FILE;
            t.file_data = data;
            t.file_size = fsz;
            strncpy(t.filename, basename, MAX_FILENAME_LEN - 1);

            printf("--- output ---\n");
            int rc = dispatch(&t, g_local_ip, g_own_port);
            printf("--- exit code: %d ---\n\n", rc);
            free(data);
            continue;
        }

        printf("Unknown command. Type 'peers', 'run <cmd>', 'submit <file.c>', or 'quit'.\n");
    }
}

/* ─── usage ──────────────────────────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--port <N>] [--iface <name>]\n"
            "  --port  <N>    TCP worker port (default: %d)\n"
            "  --iface <name> Network interface for IP detection (default: auto)\n",
            prog, DEFAULT_WORKER_PORT);
    exit(EXIT_FAILURE);
}

/* ─── main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    uint16_t port  = DEFAULT_WORKER_PORT;
    char    *iface = NULL;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc) {
            iface = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
        }
    }

    g_own_port = port;

    /* Determine local IP */
    if (get_local_ip(iface, g_local_ip) < 0) {
        fprintf(stderr, "Warning: could not determine local IP, using 127.0.0.1\n");
        strncpy(g_local_ip, "127.0.0.1", INET_ADDRSTRLEN);
    }
    printf("[p2p] Local IP: %s, Worker port: %u\n", g_local_ip, g_own_port);

    /* Signal handling */
    signal(SIGINT,  on_sigint);
    signal(SIGPIPE, SIG_IGN);

    /* Initialize subsystems */
    peer_list_init();
    worker_start(g_own_port);
    discovery_start(g_own_port, g_local_ip);

    /* Start load responder */
    pthread_t lrt;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&lrt, &attr, load_responder_thread, NULL);
    pthread_attr_destroy(&attr);

    /* Brief pause to let discovery settle */
    printf("[p2p] Waiting for peer discovery...\n");
    sleep(2);

    /* Enter REPL */
    run_repl();

    /* Graceful shutdown */
    discovery_send_bye();
    printf("[p2p] Goodbye.\n");
    return 0;
}
