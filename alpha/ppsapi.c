/***********************************************************************
 *                                                                     *
 * Copyright (c) David L. Mills 2000                                   *
 *                                                                     *
 * Permission to use, copy, modify, and distribute this software and   *
 * its documentation for any purpose and without fee is hereby         *
 * granted, provided that the above copyright notice appears in all    *
 * copies and that both the copyright notice and this permission       *
 * notice appear in supporting documentation, and that the name        *
 * University of Delaware not be used in advertising or publicity      *
 * pertaining to distribution of the software without specific,        *
 * written prior permission. The University of Delaware makes no       *
 * representations about the suitability this software for any         *
 * purpose. It is provided "as is" without express or implied          *
 * warranty.                                                           *
 *                                                                     *
 **********************************************************************/

/*
 * Modification history timepps.h
 *
 * 11 Sep 00	David L. Mills
 *	New file
 *
 *  ppsapi - pulse-per-second application program interface 
 */
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/user.h>
#include <sys/sysconfig.h>
#include <sys/tty.h>

#ifndef	__STDC__		/* what a crock */_
#define __STDC__		/* kernel and daemon use same ioctls */
#endif
#include <sys/timepps.h>

#define PPS_NORMALIZE(x) 	/* normalize timespec */ \
	do { \
		if ((x).tv_nsec >= PPS_NANOSECOND) { \
			(x).tv_nsec -= PPS_NANOSECOND; \
			(x).tv_sec++; \
		} else if ((x).tv_nsec < 0) { \
			(x).tv_nsec += PPS_NANOSECOND; \
			(x).tv_sec--; \
		} \
	} while (0)

/*
 * Persistent state variables
 */
static int pps_edge[4] = {0, 0, 0, 0}; /* last DCD transition */
int pps_unit = -1;	/* unit hardpps enable */
pps_params_t pps_params[4] = { /* unit parameters */
	{PPS_API_VERS_1, 0, {0, 0}, {0, 0}},
	{PPS_API_VERS_1, 0, {0, 0}, {0, 0}},
	{PPS_API_VERS_1, 0, {0, 0}, {0, 0}},
	{PPS_API_VERS_1, 0, {0, 0}, {0, 0}}
};
pps_info_t pps_info[4];	/* unit timestamps */

int qqs0, qqs1, qqs2;

/*
 * Ioctl processing
 */
int
ppsioctl(tp, cmd, data, flag)
	struct tty *tp;
	unsigned int cmd;
	caddr_t data;
	int flag;
{
	int unit;

	unit = minor(tp->t_dev) & 0x3;
qqs2 = unit;

	switch (cmd) {

	/*
	 * These are no-ops down here in the basement
	 */
	case PPS_IOC_GETCAP:
	case PPS_IOC_CREATE:
	case PPS_IOC_DESTROY:

	/*
	 * Set parameters for unit
	 */
	case PPS_IOC_SETPARAMS: {
		pps_params_t *t = (pps_params_t *)data;

		bcopy(t, &pps_params[unit], sizeof(pps_params_t));
		break;
	}

	/*
	 * Get parameters for unit
	 */
	case PPS_IOC_GETPARAMS: {
		pps_params_t *t = (pps_params_t *)data;

		bcopy(&pps_params[unit], t, sizeof(pps_params_t));
		break;
	}

	/*
	 * Fetch pps_info structure for unit
	 */
	case PPS_IOC_FETCH: {
		pps_info_t *t = (pps_info_t *)data;

		bcopy(&pps_info[unit], t, sizeof(pps_info_t));
		break;
	}

	/*
	 * Enable/disable kernel PPS
	 */
	case PPS_IOC_KPCBIND:
		if (*(int *)data)
			pps_unit = unit;
		else
			pps_unit = -1;
		break;

	default:
		return (-1);
	}
	return (0);
}


/*
 * Modem control transition procedure (Alpha)
 */
void
pps_dcd(unit, edge)
	int unit;			/* unit number (ace) */
	int edge;			/* mask on the MSR DCD bit */
{
	long nsec;			/* nanosecond counter */

	/*
	 * This code captures an assert/clear timestamp at each
	 * assert/clear transition of the PPS signal, regardless of the
	 * mode bits. The hardpps() routine is called on the specified
	 * edge with a pointer to the timestamp, as well as the hardware
	 * nanosecond counter value at the capture.
	 */

if (unit == 0)
	qqs0++;
if (unit == 1)
	qqs1++;

	if (edge && !pps_edge[unit]) {
		pps_info[unit].assert_sequence++;
		nsec = nano_time(&pps_info[unit].assert_timestamp);
		if (pps_params[unit].mode & PPS_OFFSETASSERT) {
			pps_info[unit].assert_timestamp.tv_sec +=
			    pps_params[unit].assert_offset.tv_sec;
			pps_info[unit].assert_timestamp.tv_nsec +=
			    pps_params[unit].assert_offset.tv_nsec;
			PPS_NORMALIZE(pps_info[unit].assert_timestamp);
		}
		if (unit == pps_unit) {
			if (pps_params[unit].mode & PPS_CAPTUREASSERT)
				hardpps(&pps_info[unit].assert_timestamp,
				    nsec);
		}
	} else if (!edge && pps_edge[unit]) {
		pps_info[unit].clear_sequence++;
		nsec = nano_time(&pps_info[unit].clear_timestamp);
		if (pps_params[unit].mode & PPS_OFFSETCLEAR) {
			pps_info[unit].clear_timestamp.tv_sec +=
			    pps_params[unit].clear_offset.tv_sec;
			pps_info[unit].clear_timestamp.tv_nsec +=
			    pps_params[unit].clear_offset.tv_nsec;
			PPS_NORMALIZE(pps_info[unit].clear_timestamp);
		}
		if (unit == pps_unit) {
			if (pps_params[unit].mode & PPS_CAPTURECLEAR)
				hardpps(&pps_info[unit].clear_timestamp,
				    nsec);
		}
	}
	pps_edge[unit] = edge;
}
