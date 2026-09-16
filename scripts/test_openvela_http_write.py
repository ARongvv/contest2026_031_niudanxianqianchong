#!/usr/bin/env python3
"""Exercise production HTTP/TLS write code with injected socket outcomes.

No credentials, network, board or firmware compiler are required. The fixture
checks framing and exact bytes across partial writes and WANT_* retries, and
uses a fake clock to check that header/body share one deadline.
"""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "packages/cAGENT/src/runtime/runtime_openvela.c"

FIXTURE = r'''
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

#define AGENT_OK 0
#define AGENT_ERROR_NETWORK -10
#define AGENT_ERROR_TIMEOUT -11
#define AGENT_ERROR_NOMEM -12
#define AGENT_ERROR_LIMIT -13
#define MBEDTLS_ERR_SSL_WANT_READ -100
#define MBEDTLS_ERR_SSL_WANT_WRITE -101
#define CAGENT_OV_TLS_HDR_BUF_SIZE 4096u
#define CAGENT_OV_TLS_WRITE_CHUNK_SIZE 1024u
#define CAGENT_OV_SOCKET_TIMEOUT_SEC 60

typedef struct { int ssl; struct { int fd; } net; } ov_tls_ctx_t;
static uint64_t now;
static int mode, writes, polls;
static const unsigned char *pending;
static size_t pending_len;
static unsigned char sent[8192];
static size_t sent_len;
static unsigned int timeouts[128], timeout_count;
static void ov_tls_diag(const char *format, ...) { (void)format; }
static void ov_tls_diag_mbed(const char *phase, int ret) {
    (void)phase; (void)ret;
}
#define ov_mem_region_log(phase, p) ((void)(phase), (void)(p))
static uint64_t ov_tls_monotonic_ms(void) { return now; }
static uint32_t ov_tls_remaining_ms(uint64_t deadline) {
    return deadline > now ? (uint32_t)(deadline - now) : 0;
}
static int mock_setsockopt(int fd, int level, int option,
                          const void *value, socklen_t size) {
    const struct timeval *tv = value;
    assert(fd == 5 && level == SOL_SOCKET && option == SO_SNDTIMEO);
    assert(size == sizeof(*tv) && timeout_count < 128);
    unsigned int ms = (unsigned int)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
    assert(ms > 0);
    if (timeout_count) assert(ms <= timeouts[timeout_count - 1]);
    timeouts[timeout_count++] = ms;
    if (mode == 7) { errno = EINVAL; return -1; }
    return 0;
}
static int mock_poll(struct pollfd *fds, nfds_t count, int timeout) {
    assert(count == 1 && fds->fd == 5 && timeout > 0);
    polls++;
    assert(fds->events == (mode == 2 ? POLLIN : POLLOUT));
    if (mode == 3) { now += timeout; return 0; }
    if (mode == 9) { fds->revents = POLLERR; return 1; }
    now += 10;
    fds->revents = fds->events;
    return 1;
}
static int mbedtls_ssl_write(int *ssl, const unsigned char *data, size_t len) {
    (void)ssl;
    assert(len > 0 && len <= CAGENT_OV_TLS_WRITE_CHUNK_SIZE);
    writes++;
    if (mode == 4) return -999;
    if (mode == 8) return 0;
    if (mode == 1 || mode == 2 || mode == 3 || mode == 9) {
        if (!pending) {
            pending = data;
            pending_len = len;
            return mode == 2 ? MBEDTLS_ERR_SSL_WANT_READ :
                               MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        /* mbedTLS requires the same arguments when retrying WANT_*. */
        assert(data == pending && len == pending_len);
        pending = NULL;
    }
    if (mode == 5 && len > 100) len = 100;
    assert(sent_len + len <= sizeof(sent));
    memcpy(sent + sent_len, data, len);
    sent_len += len;
    now += mode == 6 ? 10000 : 1;
    return (int)len;
}
#define setsockopt mock_setsockopt
#define poll mock_poll
'''

MAIN = r'''
static void reset(int selected) {
    mode = selected;
    now = 100;
    writes = polls = 0;
    pending = NULL;
    pending_len = sent_len = timeout_count = 0;
    memset(sent, 0, sizeof(sent));
}
int main(void) {
    ov_tls_ctx_t ctx = {.net.fd = 5};
    char body[5312];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (char)(i % 251);
    for (int scenario = 0; scenario <= 9; scenario++) {
        reset(scenario);
        int ret = ov_tls_write_request(&ctx, "POST", "test.invalid", "/test",
                                       "Content-Type: application/json\r\n",
                                       body, sizeof(body), 30000);
        if (scenario == 3 || scenario == 6) {
            assert(ret == AGENT_ERROR_TIMEOUT);
            assert(now == 30100);
            if (scenario == 3) assert(writes == 1 && polls == 1);
            if (scenario == 6) assert(writes == 3);
        } else if (scenario == 4 || scenario == 7 || scenario == 8 ||
                   scenario == 9) {
            assert(ret == AGENT_ERROR_NETWORK);
        } else {
            assert(ret == AGENT_OK);
            char *end = strstr((char *)sent, "\r\n\r\n");
            assert(end && strstr((char *)sent, "Content-Length: 5312\r\n"));
            size_t header_size = (size_t)(end + 4 - (char *)sent);
            assert(sent_len == header_size + sizeof(body));
            assert(memcmp(sent + header_size, body, sizeof(body)) == 0);
            if (scenario == 0) assert(writes == 7 && polls == 0);
            if (scenario == 1 || scenario == 2)
                assert(writes == 14 && polls == 7 && pending == NULL);
        }
    }
    reset(0);
    assert(ov_tls_write_request(&ctx, "GET", "test.invalid", "/", NULL,
                                NULL, 0, 30000) == AGENT_OK);
    assert(writes == 1);
    reset(0);
    now = 0;
    assert(ov_tls_write_request(&ctx, "GET", "test.invalid", "/", NULL,
                                NULL, 0, 30000) == AGENT_ERROR_TIMEOUT);
    assert(writes == 0);
    return 0;
}
'''


def main():
    source = SOURCE.read_text()
    functions = []
    for name in ("ov_tls_wait_fd", "ov_tls_wait_io", "ov_tls_write_all",
                 "ov_tls_write_request"):
        match = re.search(r"^static int " + name + r"\([^;]*?^\{.*?^\}",
                          source, re.M | re.S)
        assert match, name
        functions.append(match[0])
    with tempfile.TemporaryDirectory(prefix="openvela-http-write-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(FIXTURE + "\n".join(functions) + MAIN)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
    print("PASS: HTTP framing, 5312-byte body in 1024-byte writes, partial "
          "writes, WANT_READ/WRITE argument preservation, shared deadline, "
          "socket errors, zero progress and clock failure")


if __name__ == "__main__":
    main()
