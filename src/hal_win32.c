/* so101_hal on Windows: the servo bus's USB serial port (COMx) at 1 Mbaud, raw, and the monotonic clock.
 * The counterpart of hal_posix.c. Synchronous I/O: reads return at once with what the driver holds and
 * poll until the timeout; sleeps use a high-resolution waitable timer and spin the last stretch, since
 * Windows' default timer granularity (about 15.6 ms) is coarser than a move's 10-20 ms ticks. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "so101ik.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef struct {
    HANDLE port, timer;
    LARGE_INTEGER freq;
} win32_t;

static uint64_t now_us(void *ctx) {
    win32_t *p = ctx;
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart / p->freq.QuadPart) * 1000000u +
           (uint64_t)((c.QuadPart % p->freq.QuadPart) * 1000000 / p->freq.QuadPart);
}

static void sleep_us(void *ctx, uint32_t us) {
    win32_t *p = ctx;
    uint64_t end = now_us(ctx) + us;
    if (us > 1500 && p->timer) {   /* the timer for all but the last millisecond, then spin */
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(us - 1000) * 10;   /* relative, 100 ns units */
        if (SetWaitableTimer(p->timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(p->timer, INFINITE);
    }
    while (now_us(ctx) < end) {
        if (end - now_us(ctx) > 200) SwitchToThread();
    }
}

static int w_write(void *ctx, const uint8_t *buf, int n) {
    win32_t *p = ctx;
    int done = 0;
    while (done < n) {
        DWORD k = 0;
        if (!WriteFile(p->port, buf + done, (DWORD)(n - done), &k, NULL)) return -1;
        if (k == 0) return -1;
        done += (int)k;
    }
    return done;
}

static int w_read(void *ctx, uint8_t *buf, int n, uint32_t timeout_us) {
    win32_t *p = ctx;
    uint64_t end = now_us(ctx) + timeout_us;
    int got = 0;
    while (got < n) {
        DWORD k = 0;   /* timeouts {MAXDWORD, 0, 0}: returns at once with what is there */
        if (!ReadFile(p->port, buf + got, (DWORD)(n - got), &k, NULL)) break;
        got += (int)k;
        if (got >= n || now_us(ctx) >= end) break;
        if (k == 0) SwitchToThread();
    }
    return got;
}

static void w_drain(void *ctx) {
    win32_t *p = ctx;
    PurgeComm(p->port, PURGE_RXCLEAR);
    uint8_t junk[64];
    DWORD k = 0;
    while (ReadFile(p->port, junk, sizeof junk, &k, NULL) && k > 0) {
    }
}

int so101_hal_win32_open(so101_hal *hal, const char *port) {
    memset(hal, 0, sizeof *hal);
    char path[64];
    if (!strncmp(port, "\\\\.\\", 4)) snprintf(path, sizeof path, "%s", port);
    else snprintf(path, sizeof path, "\\\\.\\%s", port);   /* COM10 and up need the \\.\ prefix */
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return SO101_EIO;
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    COMMTIMEOUTS to = {MAXDWORD, 0, 0, 0, 0};
    if (!GetCommState(h, &dcb)) goto fail;
    dcb.BaudRate = 1000000;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb) || !SetCommTimeouts(h, &to)) goto fail;
    SetupComm(h, 4096, 4096);
    win32_t *p = calloc(1, sizeof *p);
    if (!p) goto fail;
    p->port = h;
    QueryPerformanceFrequency(&p->freq);
    p->timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!p->timer) p->timer = CreateWaitableTimerW(NULL, TRUE, NULL);   /* before Windows 10 1803 */
    timeBeginPeriod(1);
    hal->ctx = p;
    hal->write = w_write;
    hal->read = w_read;
    hal->drain = w_drain;
    hal->now_us = now_us;
    hal->sleep_us = sleep_us;
    w_drain(p);
    return SO101_OK;
fail:
    CloseHandle(h);
    return SO101_EIO;
}

void so101_hal_win32_close(so101_hal *hal) {
    win32_t *p = hal->ctx;
    if (!p) return;
    CloseHandle(p->port);
    if (p->timer) CloseHandle(p->timer);
    timeEndPeriod(1);
    free(p);
    hal->ctx = NULL;
}
#endif
