/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Web panel tests: a client that stalls must not hold the daemon, which serves the
 * panel from its main loop. Build and run with: bash test/run.sh */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "web.h"

static int g_failed, g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

/* A games list far larger than the socket buffers. */
void app_games_json(buf_t *out) {
    static char chunk[65536];
    memset(chunk, 'x', sizeof(chunk));
    for (int i = 0; i < 512; i++)
        buf_put(out, chunk, sizeof(chunk));
}
int app_add(const char *tid, char *msg, size_t cap) { return snprintf(msg, cap, "%s", tid) < 0; }
int app_remove(const char *tid, char *msg, size_t cap) { return snprintf(msg, cap, "%s", tid) < 0; }
void app_rescan(void) {}
void app_stop(void) {}
int app_restore(const char *tid, char *msg, size_t cap) { return snprintf(msg, cap, "%s", tid ? tid : "all") < 0; }
int app_restored_forget(char *msg, size_t cap) { return snprintf(msg, cap, "forgotten") < 0; }
void app_config_json(buf_t *out) { buf_printf(out, "{}"); }
int app_config_set(const char *body, char *msg, size_t cap) { return snprintf(msg, cap, "%s", body) < 0; }
int app_icon(const char *tid, buf_t *out) { return tid && out ? -1 : -1; }

static int connect_panel(void) {
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(WEB_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (c >= 0 && connect(c, (struct sockaddr *)&a, sizeof(a))) {
        close(c);
        return -1;
    }
    return c;
}

/* Seconds web_serve took for the connection, or -1 when it was still busy after limit. */
static int serve_seconds(int listen_fd, int limit) {
    time_t start = time(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        web_serve(listen_fd);
        _exit(0);
    }
    int status;
    while (time(NULL) - start <= limit) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return (int)(time(NULL) - start);
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -1;
}

static void test_stalled_reader(int listen_fd) {
    printf("client that stops reading\n");
    int c = connect_panel();
    CHECK(c >= 0);
    const char req[] = "GET /api/games HTTP/1.1\r\n\r\n";
    CHECK(write(c, req, sizeof(req) - 1) == (ssize_t)(sizeof(req) - 1));
    int s = serve_seconds(listen_fd, 15);
    printf("  served in %d s\n", s);
    CHECK(s >= 0 && s <= 12);
    close(c);
}

static void test_silent_client(int listen_fd) {
    printf("client that sends nothing\n");
    int c = connect_panel();
    CHECK(c >= 0);
    int s = serve_seconds(listen_fd, 10);
    printf("  served in %d s\n", s);
    CHECK(s >= 0 && s <= 3);
    close(c);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int listen_fd = web_listen();
    CHECK(listen_fd >= 0);
    if (listen_fd >= 0) {
        test_stalled_reader(listen_fd);
        test_silent_client(listen_fd);
        close(listen_fd);
    }
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
