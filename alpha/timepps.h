/***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 1999-2000			       *
 *								       *
 * Permission to use, copy, modify, and distribute this software and   *
 * its documentation for any purpose and without fee is hereby	       *
 * granted, provided that the above copyright notice appears in all    *
 * copies and that both the copyright notice and this permission       *
 * notice appear in supporting documentation, and that the name	       *
 * University of Delaware not be used in advertising or publicity      *
 * pertaining to distribution of the software without specific,	       *
 * written prior permission. The University of Delaware makes no       *
 * representations about the suitability this software for any	       *
 * purpose. It is provided "as is" without express or implied          *
 * warranty.							       *
 *								       *
 **********************************************************************/

/*
 * This header file complies with "Pulse-Per-Second API for UNIX-like
 * Operating Systems, Version 1.0", rfc2783. Credit is due Jeff Mogul
 * and Marc Brett, from whom much of this code was shamelessly stolen.
 */
#ifndef _SYS_TIMEPPS_H_
#define _SYS_TIMEPPS_H_

#include <sys/ioctl.h>

/*
 * The following definitions are architecture independent
 */
#define PPS_API_VERS_1	1	/* API version number */
#define PPS_JAN_1970	2208988800UL /* 1970 - 1900 in seconds */
#define PPS_NANOSECOND	1000000000L /* one nanosecond in decimal */
#define PPS_FRAC	4294967296. /* 2^32 as a double */
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
#define PPS_TSPECTONTP(x)	/* convert timespec to l_fp */ \
	do { \
		double d_temp; \
 	\
		(x).lfp_ui += (unsigned int)PPS_JAN_1970; \
		d_temp = (x).lfp_uf * PPS_FRAC / PPS_NANOSECOND; \
		if (d_temp >= FRAC) \
			(x).lfp_ui++; \
		(x).lfp_uf = (unsigned int)d_temp; \
	} while (0)

/*
 * Device/implementation parameters (mode)
 */
#define PPS_CAPTUREASSERT	0x01	/* rising edge */
#define PPS_CAPTURECLEAR	0x02	/* falling edge */
#define PPS_CAPTUREBOTH		0x03	/* both edges */
#define PPS_OFFSETASSERT	0x10	/* offset rising edge */
#define PPS_OFFSETCLEAR		0x20	/* offset falling edge */
#define PPS_OFFSET_BOTH		0x30	/* both offset edges */
#define PPS_CANWAIT		0x100	/* interface can wait */
#define PPS_CANPOLL		0x200	/* interfacc can poll */

/*	
 * Kernel actions (mode)
 */
#define PPS_ECHOASSERT		0x40	/* echo on rising edge */
#define PPS_ECHOCLEAR		0x80	/* echo on falling edge */

/*
 * Timestamp formats (tsformat)
 */
#define PPS_TSFMT_TSPEC		0x1000	/* timespec format */
#define PPS_TSFMT_NTPFP		0x2000	/* NTP format */

/*
 * Kernel discipline actions (not used)
 */
#define PPS_KC_HARDPPS		0	/* enable kernel consumer */
#define PPS_KC_HARDPPS_PLL	1	/* phase-lock mode */
#define PPS_KC_HARDPPS_FLL	2	/* frequency-lock mode */

/*
 * IOCTL definitions (see below)
 */
#ifdef __STDC__
#define PPS_IOC_CREATE		_IO('1', 1)
#define PPS_IOC_DESTROY		_IO('1', 2)
#define PPS_IOC_SETPARAMS	_IOW('1', 3, pps_params_t)
#define PPS_IOC_GETPARAMS	_IOR('1', 4, pps_params_t)
#define PPS_IOC_GETCAP		_IOR('1', 5, int)
#define PPS_IOC_FETCH		_IOR('1', 6, pps_info_t)
#define PPS_IOC_KPCBIND		_IOW('1', 7, int)
#else
#define PPS_IOC_CREATE		_IO(1, 1)
#define PPS_IOC_DESTROY		_IO(1, 2)
#define PPS_IOC_SETPARAMS	_IOW(1, 3, pps_params_t)
#define PPS_IOC_GETPARAMS	_IOR(1, 4, pps_params_t)
#define PPS_IOC_GETCAP		_IOR(1, 5, int)
#define PPS_IOC_FETCH		_IOR(1, 6, pps_info_t)
#define PPS_IOC_KPCBIND		_IOW(1, 7, int)
#endif

/*
 * Type definitions
 */
typedef unsigned pps_seq_t;	/* sequence number */

typedef struct ntp_fp {		/* NTP timestamp format */
	unsigned int lfp_ui;	/* seconds units */
	unsigned int lfp_uf;	/* seconds fraction */
} ntp_fp_t;

typedef union pps_timeu {	/* timestamp format */
	struct timespec	tspec;
	ntp_fp_t ntpfp;
	unsigned long longpad[3];
} pps_timeu_t;

/*
 * Timestamp information structure
 */
typedef struct {
	pps_seq_t assert_sequence; /* assert sequence number */
	pps_seq_t clear_sequence; /* clear sequence number */
	pps_timeu_t assert_tu;	/* assert timestamp */
	pps_timeu_t clear_tu;	/* clear timestamp */
	int current_mode;	/* current mode bits */
} pps_info_t;

#define assert_timestamp        assert_tu.tspec
#define clear_timestamp         clear_tu.tspec

#define assert_timestamp_ntpfp  assert_tu.ntpfp
#define clear_timestamp_ntpfp   clear_tu.ntpfp

/*
 * Parameter structure
 */
typedef struct {		/* parameter structure */
	int api_version;	/* API version number */
	int mode;		/* mode bits */
	pps_timeu_t assert_off_tu; /* assert offset */
	pps_timeu_t clear_off_tu; /* clear offset */
} pps_params_t;

#define assert_offset		assert_off_tu.tspec
#define clear_offset		clear_off_tu.tspec

#define assert_offset_ntpfp	assert_off_tu.ntpfp
#define clear_offset_ntpfp	clear_off_tu.ntpfp

/*
 * The following definitions are architecture-dependent for
 *
 * OSF1 churchy.udel.edu V4.0 878 alpha
 */
#define PPS_CAP		(PPS_CAPTUREBOTH | PPS_OFFSET_BOTH | \
    PPS_TSFMT_TSPEC | PPS_TSFMT_NTPFP) /* what we can do */
#define PPSDISC 12		/* this should be in <sys/ioctl.h> */

typedef struct {
	int filedes;		/* file descriptor */
	int linedisc;		/* line discipline */
	pps_params_t params;	/* PPS parameters set by user */
} pps_unit_t;
typedef pps_unit_t* pps_handle_t; /* pps handlebars */

#if !defined(KERNEL)

/*
 * Create PPS API
 */
static __inline int
time_pps_create(
	int filedes,		/* file descriptor */
	pps_handle_t *handle	/* ppsapi handle */
	)
{
	int line = PPSDISC;

	/*
	 * Check for valid arguments and attach PPS signal.
	 */
	if (!handle) {
		errno = EFAULT;
		return (-1);	/* null pointer */
	}

	/*
	 * Allocate and initialize default unit structure.
	 */
	*handle = malloc(sizeof(pps_unit_t));
	if (!(*handle)) {
		errno = EBADF;
		return (-1);	/* what, no memory? */
	}
	memset(*handle, 0, sizeof(pps_unit_t));
	(*handle)->filedes = filedes;
	(*handle)->params.api_version = PPS_API_VERS_1;
	(*handle)->params.mode = PPS_CAPTUREASSERT | PPS_TSFMT_TSPEC;
	if (ioctl(filedes, TIOCGETD, &(*handle)->linedisc) < 0) {
		free(handle);
		return (-1);
	}
	if (ioctl(filedes, TIOCSETD, &line) < 0) {
		return (-1);
		free(handle);
	}
	return (0);
}

/*
 * Destroy PPS API
 */
static __inline int
time_pps_destroy(
	pps_handle_t handle	/* ppsapi handle */
	)
{
	/*
	 * Check for valid arguments and detach PPS signal.
	 */
	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	ioctl(handle->filedes, TIOCSETD, &handle->linedisc);
	free(handle);
	return (0);
}

/*
 * Set parameters
 */
static __inline int
time_pps_setparams(
	pps_handle_t handle,	/* ppsapi handle */
	const pps_params_t *params /* ppsapi parameters */
	)
{
	/*
	 * Check for valid arguments and set parameters.
	 */
	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	if (!params) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}
	memcpy(&handle->params, params, sizeof(pps_params_t));
	handle->params.api_version = PPS_API_VERS_1;
	return (0);
}

/*
 * Get parameters
 */
static __inline int
time_pps_getparams(
	pps_handle_t handle,	/* ppsapi handle */
	pps_params_t *params	/* ppsapi parameters */
	)
{
	/*
	 * Check for valid arguments and get parameters.
	 */
	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	if (!params) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}
	memcpy(params, &handle->params, sizeof(pps_params_t));
	return (0);
}

/*
 * Get capabilities
 */
static __inline int 
time_pps_getcap(
	pps_handle_t handle,	/* ppsapi handle */
	int *mode		/* capture mode */
	)
{
	/*
	 * Check for valid arguments and get capabilities.
	 */
	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	if (!mode) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}
	*mode = PPS_CAP;
	return (0);
}

/*
 * Fetch timestamps
 */
static __inline int
time_pps_fetch(
	pps_handle_t handle,	/* ppsapi handle */
	const int tsformat,	/* format selector */
	pps_info_t *ppsinfo,	/* what we want */
 	const struct timespec *timeout /* how long to wait for it */
	)
{
	pps_info_t infobuf;
	int tsf;

	/*
	 * Check for valid arguments and fetch timestamps
	 */
	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	if (!ppsinfo) {
		errno = EFAULT;
		return (-1);	/* bad argument */
	}
	if (ioctl(handle->filedes, PPS_IOC_FETCH, &infobuf) < 0)
		return (-1);
	tsf = tsformat;
	if (tsf == 0)
		tsf = handle->params.mode & (PPS_TSFMT_TSPEC |
		     PPS_TSFMT_NTPFP);

	/*
	 * Apply offsets as specified
	 */
	if (handle->params.mode & PPS_OFFSETASSERT) {
		infobuf.assert_timestamp.tv_sec +=
		    handle->params.assert_offset.tv_sec;
		infobuf.assert_timestamp.tv_nsec +=
		    handle->params.assert_offset.tv_nsec;
		PPS_NORMALIZE(infobuf.assert_timestamp);
	}
	if (handle->params.mode & PPS_OFFSETCLEAR) {
		infobuf.clear_timestamp.tv_sec +=
		    handle->params.clear_offset.tv_sec;
		infobuf.clear_timestamp.tv_nsec +=
		    handle->params.clear_offset.tv_nsec;
		PPS_NORMALIZE(infobuf.clear_timestamp);
	}

	/*
	 * Translate to specified format
	 */
	switch (tsf) {

	/*
	 * timespec format requires no translation
	 */
	case PPS_TSFMT_TSPEC:
		break;

	/*
	 * NTP format requires conversion to fraction form
	 */
	case PPS_TSFMT_NTPFP:
		PPS_TSPECTONTP(infobuf.assert_timestamp_ntpfp);
		PPS_TSPECTONTP(infobuf.clear_timestamp_ntpfp);
		break;

	default:
		errno = EINVAL;
		return (-1);
	}
	infobuf.current_mode = handle->params.mode;
	memcpy(ppsinfo, &infobuf, sizeof(pps_info_t));
	return (0);
}

/*
 * Define PPS discipline and mode
 */
static __inline int
time_pps_kcbind(
	pps_handle_t handle,	/* ppsapi handle */
	const int kernel_consumer, /* consumer identifier */
	const int mode,		/* capture mode */
	const int tsformat	/* timestamp format */
	)
{
	/*
	 * Check for valid arguments and bind kernel consumer
	 */
	if (!handle) {
		errno = EBADF;
		return (-1);	/* bad handle */
	}
	if (geteuid() != 0) {
		errno = EPERM;
		return (-1);	/* must be superuser */
	}

	/*
	 * Move the offsets to the kernel and zero the ones here. Only
	 * the superuser can do this, so others sharing the same device
	 * may suffer.
	 */
	if (ioctl(handle->filedes, PPS_IOC_SETPARAMS, &handle->params) <
	    0)
		return (-1);
	return (ioctl(handle->filedes, PPS_IOC_KPCBIND, &mode));
}

#endif /* !KERNEL */
#endif /* _SYS_TIMEPPS_H_ */
