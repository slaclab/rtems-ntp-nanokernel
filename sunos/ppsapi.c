/*
 *  ppsapi STREAMS module
 *
 * This module draws weightily from the ppsclock STREAMS module built by
 * Craig Leres at LBL. Credit is due.
 */

#include <string.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/timepps.h>
#include <sys/ppsclock.h>
#include <sys/tty.h>
#include <sys/ttycom.h>
#include <sys/user.h>

/*
 * SunOS 4.1.3 serial driver
 */
#include <sundev/zsreg.h>
#include <sundev/zscom.h>

/*
 * Function prototypes
 */
static int ppsapi_open();	/* STREAMS open */
static int ppsapi_close();	/* STREAMS close */
static int ppsapi_wput();	/* STREAMS write */
static void ppsapi_intr();	/* PPS interrupt processor (SunOS) */
extern hardpps();		/* PPS signal processor */

/*
 * STREAMS interface
 */
static struct module_info stm_info = {
	0x434c,			/* module id number (??) */
	PPSAPISTR,		/* module name */
	0,			/* minimum packet size */
	INFPSZ,			/* infinite maximum packet size */
	STRHIGH,		/* hi-water mark */
	STRLOW,			/* lo-water mark */
};

static struct qinit ppsapi_rinit = {
	(int (*)())ppsapi_wput, /* put procedure */
	NULL,			/* service procedure */
	(int (*)())ppsapi_open,	/* open procedure */
	(int (*)())ppsapi_close, /* close procedure */
	NULL,			/* administration procedure */
	&stm_info,		/* module information structure */
	NULL			/* module statistics structure */
};

static struct qinit ppsapi_winit = {
	(int (*)())ppsapi_wput, /* put procedure */
	NULL,			/* service procedure */
	(int (*)())ppsapi_open,	/* open procedure */
	(int (*)())ppsapi_close, /* close procedure */
	NULL,			/* administration procedure */
	&stm_info,		/* module information structure */
	NULL			/* module statistics structure */
};

struct streamtab ppsapi_info = {
	&ppsapi_rinit,		/* qinit for read side */
	&ppsapi_winit,		/* qinit for write side */
	NULL,			/* mux qinit for read */
	NULL			/* mux qinit for write */
};

/*
 * SunOS 4.1.3 serial driver
 */
static struct zsops *ppssavedzsops;
static struct zsops ppszsops;
static struct zscom *ppssavedzscom;
#ifdef OPENPROMS
extern struct zsaline *zsaline;
#else
extern struct zsaline zsaline[];
#endif

/*
 * Persistent state variables
 */
static struct ppsclockev ppsclockev; /* PPS time structure */
static pps_info_t pps_info;	/* API info structure */
static int pps_kpcmode;		/* kernel PPS mode */

/*
 * Open procedure called by I_PUSH
 */
static int
ppsapi_open(q, dev, flag, sflag)
	register queue_t *q;
	register dev_t dev;
	register int flag;
	register int sflag;
{
	register struct zsaline *za;
	register struct zscom *zs;

	/*
	 * We must be called with MODOPEN
	 */
	if (sflag != MODOPEN)
		return (OPENFAIL);

	/*
	 * Hook up our external status interrupt handler
	 */
	if (ppssavedzsops == NULL) {
		za = &zsaline[minor(dev) & 0x7f];
		if ((zs = za->za_common) == NULL)
			return (OPENFAIL);
		ppssavedzsops = zs->zs_ops;
		ppszsops = *ppssavedzsops;
		ppszsops.zsop_xsint = (int (*)())ppsapi_intr;
		zsopinit(zs, &ppszsops);
		ppssavedzscom = zs;
	}
	return (0);
}

/*
 * Close procedure called by I_POP
 */
static int
ppsapi_close(q)
	register queue_t *q;
{

	/*
	 * Flush outstanding packets
	 */
	flushq(WR(q), FLUSHALL);

	/*
	 * Unhook our external status interrupt handler
	 */
	if (ppssavedzsops) {
		zsopinit(ppssavedzscom, ppssavedzsops);
		ppssavedzscom = NULL;
		ppssavedzsops = NULL;
	}
	return (0);
}

/*
 * Read and write put procedure. Note that we can only get ioctl
 * messages in the "write" case.
 */
static int
ppsapi_wput(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	register struct iocblk *iocp;
	register mblk_t *datap;
	pps_params_t params;

	switch (mp->b_datap->db_type) {

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW)
			flushq(q, FLUSHDATA);
		putnext(q, mp);
		break;

	case M_IOCTL:
		iocp = (struct iocblk *)mp->b_rptr;
		switch (iocp->ioc_cmd) {

		/*
		 * This is the original ppsclock interface built
		 * by Van Jacobson and Craig Leres. It is
		 * included for legacy purposes.
		 */
 		case CIOGETEV:
			datap = allocb(sizeof(struct ppsclockev),
			    BPRI_HI);
			if (datap == NULL) {
				iocp->ioc_error = ENOMEM;
				mp->b_datap->db_type = M_IOCNAK;
				qreply(q, mp);
				break;
			}
			mp->b_cont = datap;
			bcopy((char *)&ppsclockev, datap->b_wptr,
			    sizeof(struct ppsclockev));
			datap->b_wptr += sizeof(struct ppsclockev);
			iocp->ioc_count = sizeof(struct ppsclockev);
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			break;

		/*
		 * These have no meaning in this design. Just
		 * ignore them.
		 */
		case PPS_IOC_CREATE:
		case PPS_IOC_DESTROY:
		case PPS_IOC_SETPARAMS:
		case PPS_IOC_GETPARAMS:
		case PPS_IOC_GETCAP:
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			break;

		/*
		 * Fetch pps_info structure.
		 */
		case PPS_IOC_FETCH:
			datap = allocb(sizeof(pps_info_t), BPRI_HI);
			if (datap == NULL) {
				iocp->ioc_error = ENOMEM;
				mp->b_datap->db_type = M_IOCNAK;
				qreply(q, mp);
				break;
			}
			mp->b_cont = datap;
			bcopy((char *)&pps_info, datap->b_wptr,
			    sizeof(pps_info_t));
			datap->b_wptr += sizeof(pps_info_t);
			iocp->ioc_count = sizeof(pps_info_t);
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			break;

		/*
		 * Store pps_kpcmode mode bits.
		 */
		case PPS_IOC_KPCBIND:
			bcopy((char *)mp->b_cont->b_rptr,
			    (char *)&pps_kpcmode, sizeof(int));
			iocp->ioc_count = 0;
			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			break;

		/*
		 * Pass unknown ioctls downstream.
		 */
		default:
			putnext(q, mp);
		}
		break;

	/*
	 * Pass unknown messages downstream.
	 */
	default:
		putnext(q, mp);
	}
	return (0);
}

/*
 * Modem control transition procedure (SunOS)
 */
static void
ppsapi_intr(zs)
	register struct zscom *zs;
{
	register struct zsaline *za = (struct zsaline *)zs->zs_priv;
	register struct zscc_device *zsaddr = zs->zs_addr;
	register u_char s0;
	long nsec;

	/*
	 * This code captures an assert/clear timestamp at each
	 * assert/clear transition of the PPS signal, regardless of the
	 * mode bits. The hardpps() routine is called on the specified
	 * edge with a pointer to the timestamp, as well as the hardware
	 * nanosecond counter value at the capture.
	 */
	s0 = zsaddr->zscc_control;
	if ((s0 ^ za->za_rr0) & ZSRR0_CD) {
		if ((s0 & ZSRR0_CD) != 0) {
			ppsclockev.serial++;
			uniqtime(&ppsclockev.tv);
			pps_info.assert_sequence++;
			nsec = nano_time(&pps_info.assert_timestamp);
			if (pps_kpcmode & PPS_CAPTUREASSERT)
				hardpps(&pps_info.assert_timestamp,
				    nsec);
		} else {
			pps_info.clear_sequence++;
			nsec = nano_time(&pps_info.clear_timestamp);
			if (pps_kpcmode & PPS_CAPTURECLEAR)
				hardpps(&pps_info.clear_timestamp,
				    nsec);

		}

		/*
		 * Reset interrupt status (SunOS)
		 */
		za->za_rr0 = s0;
		zsaddr->zscc_control = ZSWR0_RESET_STATUS;
		return;
	}
	/*
	 * Call real external status interrupt routine
	 */
	(void)(*ppssavedzsops->zsop_xsint)(zs);
}
