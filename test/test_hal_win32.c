/* Windows only, no device needed: the HAL's clock and sleeps (a move's ticks depend on them), and that a port
 * that is not there fails cleanly. Never opens a real COM port. */
#include <stdio.h>
#include "../src/hal_win32.c"

int main(void) {
    win32_t p = {0};
    QueryPerformanceFrequency(&p.freq);
    p.timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    timeBeginPeriod(1);
    int fails = 0;
    const uint32_t asks[] = {500, 2000, 10000, 20000};
    for (int a = 0; a < 4; a++) {
        double worst = 0, sum = 0;
        for (int i = 0; i < 100; i++) {
            uint64_t t0 = now_us(&p);
            sleep_us(&p, asks[a]);
            double late = (double)(now_us(&p) - t0) - asks[a];
            if (late < 0) { printf("sleep_us(%u) returned %.0f us early\n", asks[a], -late); fails++; }
            if (late > worst) worst = late;
            sum += late;
        }
        printf("sleep_us(%5u): late by %.0f us on average, %.0f at worst\n", asks[a], sum / 100, worst);
        if (worst > 2000) { printf("  too late for 10-20 ms ticks\n"); fails++; }
    }
    so101_hal h;
    if (so101_hal_win32_open(&h, "COM250") != SO101_EIO) { printf("COM250 did not fail\n"); fails++; }
    else printf("a missing port: SO101_EIO, as it should\n");
    timeEndPeriod(1);
    printf("%d failures\n", fails);
    return fails != 0;
}
