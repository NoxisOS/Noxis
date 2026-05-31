/**
 * @file    kernel/syscall/sys_misc.c
 * @brief   Miscellaneous syscalls: time, sleep
 */
#include "syscall_internal.h"

void sys_time(isr_frame_t* frame) {
    uint32_t secs = pit_uptime_ms() / 1000;
    if (frame->ebx && _user_range_ok(frame->ebx, 4))
        *(uint32_t*)frame->ebx = secs;
    frame->eax = secs;
}

void sys_sleep(isr_frame_t* frame) {
    thread_sleep(frame->ebx);   /* milliseconds */
    frame->eax = 0;
}
