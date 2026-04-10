#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>

#include "worker.h"
#include "common.h"
#include "net_utils.h"
#include "load_monitor.h"

/* ─── Handle load request via UDP (called from dispatcher when it queries us)
   This is handled in dispatcher.c; worker handles only TCP tasks. ─────── */

/* ─── Per-connection handler ─────────────────────────────────────────────── */
typedef struct {
    int  client_fd;
    char client_ip[INET_ADDRSTRLEN];
} conn_args_t;

/*
 * Stream child output back to the sender.
 * Reads from pipe_fd (child stdout+stderr), sends MSG_OUTPUT chunks.
 * Returns child exit code.
 */
static int stream_and_wait(int client_fd, int pipe_fd, pid_t child_pid) {
    uint8_t buf[OUTPUT_BUF_SIZE];
    int child_exit = 0;

    for (;;) {
        ssize_t n = read(pipe_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break; /* pipe closed → child done */

        if (send_msg(client_fd, MSG_OUTPUT, buf, (uint32_t)n) < 0) {
            /* Sender disconnected; kill child */
            kill(child_pid, SIGKILL);
            break;
        }
    }

    int status;
    waitpid(child_pid, &status, 0);
    if (WIFEXITED(status))   child_exit = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) child_exit = 128 + WTERMSIG(status);

    return child_exit;
}

/*
 * Execute cmd (via /bin/sh -c) with merged stdout+stderr piped back.
 */
static void execute_and_stream(int client_fd, const char *cmd) {
    LOG("Executing command: %s", cmd);
    load_task_start();

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        load_task_done();
        LOG("pipe() failed: %s", strerror(errno));
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        load_task_done();
        return;
    }

    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);
    int exit_code = stream_and_wait(client_fd, pipefd[0], pid);
    close(pipefd[0]);

    payload_task_done_t done;
    done.exit_code = htonl((uint32_t)exit_code);
    send_msg(client_fd, MSG_TASK_DONE, &done, sizeof(done));

    load_task_done();
    LOG("Command done, exit=%d", exit_code);
}

/*
 * Compile a .c file to a temp binary, then execute and stream.
 */
static void compile_and_stream(int client_fd,
                                const char *filename,
                                const uint8_t *data,
                                uint32_t data_len) {
    LOG("Compiling file: %s (%u bytes)", filename, data_len);
    load_task_start();

    /* Write source to /tmp */
    char src_path[256], bin_path[256], compile_cmd[512];
    snprintf(src_path, sizeof(src_path), "/tmp/p2p_%d.c",   (int)getpid());
    snprintf(bin_path, sizeof(bin_path), "/tmp/p2p_%d_bin", (int)getpid());

    FILE *f = fopen(src_path, "wb");
    if (!f) {
        LOG("Cannot write temp source: %s", strerror(errno));
        load_task_done();
        return;
    }
    fwrite(data, 1, data_len, f);
    fclose(f);

    /* Compile */
    snprintf(compile_cmd, sizeof(compile_cmd),
             "gcc -o %s %s 2>&1", bin_path, src_path);

    /* Stream compilation output first */
    LOG("Running: %s", compile_cmd);
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            pid_t cpid = fork();
            if (cpid == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);
                execl("/bin/sh", "sh", "-c", compile_cmd, (char *)NULL);
                _exit(127);
            }
            close(pipefd[1]);
            int compile_exit = stream_and_wait(client_fd, pipefd[0], cpid);
            close(pipefd[0]);

            if (compile_exit != 0) {
                payload_task_done_t done;
                done.exit_code = htonl((uint32_t)compile_exit);
                send_msg(client_fd, MSG_TASK_DONE, &done, sizeof(done));
                unlink(src_path);
                load_task_done();
                return;
            }
        }
    }

    /* Run the compiled binary */
    {
        int pipefd[2];
        pipe(pipefd);
        pid_t rpid = fork();
        if (rpid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            execl(bin_path, bin_path, (char *)NULL);
            _exit(127);
        }
        close(pipefd[1]);
        int run_exit = stream_and_wait(client_fd, pipefd[0], rpid);
        close(pipefd[0]);

        payload_task_done_t done;
        done.exit_code = htonl((uint32_t)run_exit);
        send_msg(client_fd, MSG_TASK_DONE, &done, sizeof(done));
    }

    unlink(src_path);
    unlink(bin_path);
    load_task_done();
    LOG("Compile+run done for %s", filename);
}

/* ─── Connection handler thread ──────────────────────────────────────────── */
static void *handle_connection(void *arg) {
    conn_args_t *ca = (conn_args_t *)arg;
    int cfd = ca->client_fd;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, ca->client_ip, sizeof(client_ip));
    free(ca);

    LOG("Connection from %s", client_ip);

    msg_type_t type;
    uint8_t   *payload = NULL;
    uint32_t   plen    = 0;

    if (recv_msg(cfd, &type, &payload, &plen) < 0) {
        LOG("Failed to receive task from %s", client_ip);
        close(cfd);
        return NULL;
    }

    switch (type) {
        case MSG_TASK_CMD: {
            /* payload: uint16_t cmd_len (BE) + cmd bytes */
            if (plen < 2) break;
            uint16_t cmd_len;
            memcpy(&cmd_len, payload, 2);
            cmd_len = ntohs(cmd_len);
            if (cmd_len > MAX_CMD_LEN || (uint32_t)(cmd_len + 2) > plen) break;
            char cmd[MAX_CMD_LEN + 1];
            memcpy(cmd, payload + 2, cmd_len);
            cmd[cmd_len] = '\0';
            free(payload); payload = NULL;
            execute_and_stream(cfd, cmd);
            break;
        }
        case MSG_TASK_FILE: {
            /*
             * payload layout:
             *   uint16_t  filename_len (BE)
             *   char      filename[filename_len]
             *   uint32_t  file_len (BE)
             *   uint8_t   file_data[file_len]
             */
            if (plen < 6) break;
            uint8_t *p = payload;

            uint16_t fn_len;
            memcpy(&fn_len, p, 2); fn_len = ntohs(fn_len); p += 2;
            if (fn_len >= MAX_FILENAME_LEN || (size_t)(p - payload) + fn_len + 4 > plen) break;

            char filename[MAX_FILENAME_LEN];
            memcpy(filename, p, fn_len); filename[fn_len] = '\0'; p += fn_len;

            uint32_t file_len;
            memcpy(&file_len, p, 4); file_len = ntohl(file_len); p += 4;
            if (file_len > MAX_FILE_SIZE || (size_t)(p - payload) + file_len > plen) break;

            compile_and_stream(cfd, filename, p, file_len);
            free(payload); payload = NULL;
            break;
        }
        default:
            LOG("Unknown message type 0x%02x from %s", type, client_ip);
            break;
    }

    free(payload);
    close(cfd);
    return NULL;
}

/* ─── Accept loop thread ─────────────────────────────────────────────────── */
static void *accept_loop(void *arg) {
    int server_fd = *(int *)arg;
    free(arg);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int cfd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        /* Check concurrent task limit */
        if (load_get_active_tasks() >= MAX_CONCURRENT_TASKS) {
            LOG("At capacity (%d tasks); rejecting connection", MAX_CONCURRENT_TASKS);
            close(cfd);
            continue;
        }

        conn_args_t *ca = malloc(sizeof(conn_args_t));
        ca->client_fd   = cfd;
        inet_ntop(AF_INET, &client_addr.sin_addr, ca->client_ip, sizeof(ca->client_ip));

        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &attr, handle_connection, ca);
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

/* ─── worker_start ───────────────────────────────────────────────────────── */
void worker_start(uint16_t port) {
    int *server_fd = malloc(sizeof(int));
    *server_fd = make_tcp_server(port);
    if (*server_fd < 0) DIE("worker_start: cannot bind TCP port %u", port);

    LOG("Worker listening on TCP port %u", port);

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, accept_loop, server_fd);
    pthread_attr_destroy(&attr);
}
