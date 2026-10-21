/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The web panel server: routes, methods, title id checks and malformed requests, over a
 * real socket with the app_* calls recorded (stubs.c). Run with: bash test/run.sh */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "stubs.h"
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

static char g_resp[1 << 20];

typedef struct {
    int fd;
    const char *later;
} late_t;

static void *write_later(void *arg) {
    late_t *l = arg;
    usleep(150000);
    if (write(l->fd, l->later, strlen(l->later)) < 0)
        perror("write");
    shutdown(l->fd, SHUT_WR);
    return NULL;
}

/* Sends req (then later, after a pause, when given) and returns the full response. */
static const char *ask2(const char *req, size_t len, const char *later) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
        return "";
    int big = 1 << 20;
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &big, sizeof(big));
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &big, sizeof(big));
    if (len && write(sv[0], req, len) < 0)
        perror("write");
    pthread_t th;
    late_t l = {sv[0], later};
    if (later)
        pthread_create(&th, NULL, write_later, &l);
    else
        shutdown(sv[0], SHUT_WR);
    g_stub_call[0] = 0;
    g_stub_arg[0] = 0;
    web_handle(sv[1]);
    if (later)
        pthread_join(th, NULL);
    size_t n = 0;
    ssize_t r;
    while (n < sizeof(g_resp) - 1 && (r = read(sv[0], g_resp + n, sizeof(g_resp) - 1 - n)) > 0)
        n += (size_t)r;
    g_resp[n] = 0;
    close(sv[0]);
    return g_resp;
}

static const char *ask(const char *req) { return ask2(req, strlen(req), NULL); }

static int status(const char *resp, const char *code) {
    char want[32];
    snprintf(want, sizeof(want), "HTTP/1.1 %s", code);
    return !strncmp(resp, want, strlen(want));
}

static const char *post(const char *path, const char *body) {
    static char req[16384];
    snprintf(req, sizeof(req), "POST %s HTTP/1.1\r\nHost: x\r\nContent-Length: %zu\r\n\r\n%s", path, strlen(body), body);
    return ask(req);
}

int main(void) {
    const char *r;
    printf("pages\n");
    r = ask("GET / HTTP/1.1\r\n\r\n");
    CHECK(status(r, "200") && strstr(r, "text/html") && strstr(r, "<title>PS4Mount</title>"));
    CHECK(strstr(r, "location.replace('/legacy')") != NULL);
    r = ask("GET /legacy HTTP/1.1\r\n\r\n");
    CHECK(status(r, "200") && strstr(r, "id=\"v-pick\"") && !strstr(r, "</dialog>"));
    r = ask("GET /nothing HTTP/1.1\r\n\r\n");
    CHECK(status(r, "404"));
    r = ask("GET /api/games HTTP/1.1\r\n\r\n");
    CHECK(status(r, "200") && strstr(r, "application/json") && strstr(r, "\"games\""));
    CHECK(strstr(r, "Cache-Control: no-store") != NULL);

    printf("state changes need POST\n");
    const char *changing[] = {"/api/mount?title_id=CUSA00001", "/api/unmount?title_id=CUSA00001", "/api/rescan",
                              "/api/restore", "/api/restored/forget", "/api/stop", NULL};
    for (int i = 0; changing[i]; i++) {
        char req[256];
        snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\n\r\n", changing[i]);
        int before = g_stub_calls;
        r = ask(req);
        CHECK(status(r, "404") && g_stub_calls == before);
    }

    printf("title ids\n");
    r = post("/api/mount?title_id=CUSA00791", "");
    CHECK(status(r, "200") && !strcmp(g_stub_call, "add") && !strcmp(g_stub_arg, "CUSA00791"));
    r = post("/api/unmount?title_id=CUSA04263", "");
    CHECK(!strcmp(g_stub_call, "remove") && !strcmp(g_stub_arg, "CUSA04263"));
    const char *bad[] = {"/api/mount?title_id=CUSA0079", "/api/mount?title_id=../../../", "/api/mount?title_id=PPSA00001",
                         "/api/mount?title_id=cusa00791", "/api/mount?title_id=", "/api/mount", "/api/unmount?title_id=CUSA%2F%2E%2E",
                         "/api/mount?title_id=CUSA0079X", "/api/mount?title_id=CUSAABCDE", "/api/mount?title_id=CUSA007910",
                         NULL};
    for (int i = 0; bad[i]; i++) {
        int before = g_stub_calls;
        r = post(bad[i], "");
        CHECK(status(r, "400") && g_stub_calls == before);
    }
    /* icons are read by title id: nothing else may reach the file system */
    const char *bad_icons[] = {"/icon/../../../", "/icon/..%2F..%2F", "/icon/CUSA0079", "/icon/", "/icon/CUSA00791/../x", NULL};
    for (int i = 0; bad_icons[i]; i++) {
        char req[128];
        snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\n\r\n", bad_icons[i]);
        int before = g_stub_calls;
        r = ask(req);
        CHECK(status(r, "404") && g_stub_calls == before);
    }
    r = ask("GET /icon/CUSA00791 HTTP/1.1\r\n\r\n");
    CHECK(status(r, "200") && strstr(r, "image/png") && !strcmp(g_stub_arg, "CUSA00791"));

    printf("moving back\n");
    post("/api/restore", "");
    CHECK(!strcmp(g_stub_call, "restore") && !strcmp(g_stub_arg, "(null)")); /* all games */
    post("/api/restore?title_id=CUSA00785", "");
    CHECK(!strcmp(g_stub_arg, "CUSA00785"));
    post("/api/restore", "title_ids=CUSA00785,CUSA04263");
    CHECK(!strcmp(g_stub_arg, "CUSA00785,CUSA04263"));
    post("/api/restore", "title_ids=CUSA00785%2CCUSA04263%2cCUSA00791");
    CHECK(!strcmp(g_stub_arg, "CUSA00785,CUSA04263,CUSA00791"));
    post("/api/restore", "title_ids=CUSA00785&x=1");
    CHECK(!strcmp(g_stub_arg, "CUSA00785"));
    int before = g_stub_calls;
    r = post("/api/restorex", "");
    CHECK(status(r, "404") && g_stub_calls == before);
    r = post("/api/restore?title_id=bad", "");
    CHECK(status(r, "400"));

    printf("settings\n");
    post("/api/config", "batch=1&restore_batch=5");
    CHECK(!strcmp(g_stub_call, "config") && !strcmp(g_stub_arg, "batch=1&restore_batch=5"));
    r = ask("GET /api/config HTTP/1.1\r\n\r\n");
    CHECK(status(r, "200") && strstr(r, "\"batch\""));
    post("/api/restored/forget", "");
    CHECK(!strcmp(g_stub_call, "forget"));

    printf("a body that arrives after the headers\n");
    const char *head = "POST /api/config HTTP/1.1\r\nContent-Length: 15\r\n\r\n";
    ask2(head, strlen(head), "restore_batch=7");
    CHECK(!strcmp(g_stub_call, "config") && !strcmp(g_stub_arg, "restore_batch=7"));

    printf("malformed requests\n");
    r = ask("");
    CHECK(r[0] == 0); /* nothing sent: closed without an answer */
    r = ask("\r\n\r\n");
    CHECK(status(r, "404"));
    r = ask("POST /api/config HTTP/1.1\r\nContent-Length: -5\r\n\r\nx=1");
    CHECK(status(r, "200") || status(r, "400"));
    r = ask("POST /api/config HTTP/1.1\r\nContent-Length: 999999999999\r\n\r\nx=1");
    CHECK(status(r, "200") || status(r, "400"));
    static char huge[20000];
    memset(huge, 'A', sizeof(huge) - 1);
    memcpy(huge, "GET /", 5);
    r = ask2(huge, sizeof(huge) - 1, NULL);
    CHECK(status(r, "404"));
    char nul[] = "GET /\0api/stop HTTP/1.1\r\n\r\n";
    before = g_stub_calls;
    ask2(nul, sizeof(nul) - 1, NULL);
    CHECK(g_stub_calls == before);

    printf("answers are valid JSON strings\n");
    r = post("/api/rescan", "");
    CHECK(strstr(r, "\"message\":\"done \\\"rescan\\\"\"") != NULL || strstr(r, "Scanning") != NULL);
    r = post("/api/mount?title_id=CUSA00001", "");
    CHECK(strstr(r, "\"message\":\"done \\\"add\\\"\"") != NULL);

    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
