#ifndef RTEMS_NTP_PICTIMER_SUPPORT_H
#define RTEMS_NTP_PICTIMER_SUPPORT_H

int pictimerInstall();

int pictimerCleanup();

unsigned pictimerEnable(int v);

unsigned pictimerDisable();

#define PICTIMER_SYNC_EVENT	RTEMS_EVENT_2

#endif
