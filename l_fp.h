/***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 1993-2001			       *
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
 * purpose. It is provided "as is" without express or implied	       *
 * warranty.							       *
 *								       *
 **********************************************************************/
/*
 * Modification history time_ops.h
 *
 * 23 Oct 98    David L. Mills
 *      Created file
 *
 * This file contains macro sets for 64-bit arithmetic and logic
 * operations in both 32-bit and 64-bit architectures. They are designed
 * to use the same source code in either architecture, with all
 * differences confined to this file. Macros adapted from the NTP
 * distribution ntp_fp.h, original author Dennis Ferguson.
 */
#if !defined(NTP_L64)

#include <stdint.h>

/*
 * Double precision macros for 32-bit machines
 *
 * A 64-bit fixed-point value is represented in 32-bit architectures as
 * two 32-bit words in the following format:
 *
 *    0		    1		   2		   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |s|			Integral Part			     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			       Fractional Part			     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
#if 1
#define int32	int32_t
#define u_int32	uint32_t
#else
typedef long int32;
typedef unsigned long u_int32;
#endif
typedef struct {			/* basic type in two formats */
	union {
		u_int32 Xl_ui;
		int32 Xl_i;
	} Ul_i;
	union {
		u_int32 Xl_uf;
		int32 Xl_f;
	} Ul_f;
} l_fp;

#define l_ui	Ul_i.Xl_ui		/* unsigned integral part */
#define l_i	Ul_i.Xl_i		/* signed integral part */
#define l_uf	Ul_f.Xl_uf		/* unsigned fractional part */
#define l_f	Ul_f.Xl_f		/* signed fractional part */

#define M_ADD(r_i, r_f, a_i, a_f)	/* r += a */ \
	do { \
		register u_int32 lo_tmp; \
		register u_int32 hi_tmp; \
		\
		lo_tmp = ((r_f) & 0xffff) + ((a_f) & 0xffff); \
		hi_tmp = (((r_f) >> 16) & 0xffff) + (((a_f) >> 16) & \
		    0xffff); \
		if (lo_tmp & 0x10000) \
			 hi_tmp++; \
		(r_f) = ((hi_tmp & 0xffff) << 16) | (lo_tmp & 0xffff); \
		\
		(r_i) += (a_i); \
		if (hi_tmp & 0x10000) \
			 (r_i)++; \
	} while (0)

#define M_SUB(r_i, r_f, a_i, a_f)	/* r -= a */ \
	do { \
		register u_int32 lo_tmp; \
		register u_int32 hi_tmp; \
		\
		if ((a_f) == 0) { \
			 (r_i) -= (a_i); \
		} else { \
			lo_tmp = ((r_f) & 0xffff) + ((-((int32)(a_f))) \
			    & 0xffff); \
			hi_tmp = (((r_f) >> 16) & 0xffff) \
			    + (((-((int32)(a_f))) >> 16) & 0xffff); \
			if (lo_tmp & 0x10000) \
				 hi_tmp++; \
			(r_f) = ((hi_tmp & 0xffff) << 16) | (lo_tmp & \
			    0xffff); \
			(r_i) += ~(a_i); \
			if (hi_tmp & 0x10000) \
				 (r_i)++; \
		 } \
	} while (0)

#define M_NEG(v_i, v_f)  /* v = -v */ \
	do { \
		 if ((v_f) == 0) \
			 (v_i) = -((int32)(v_i)); \
		 else { \
			 (v_f) = -((int32)(v_f)); \
			 (v_i) = ~(v_i); \
		 } \
	} while (0)

#define M_RSHIFT(v_i, v_f, n)		/* v >>= n */ \
	do { \
		if ((v_i) < 0) { \
			M_NEG((v_i), (v_f)); \
			if ((n) < 32) { \
				(v_f) = ((v_f) >> (n)) | ((v_i) << \
				    (32 - (n))); \
				(v_i) = (v_i) >> (n); \
			} else { \
				(v_f) = (v_i) >> ((n) - 32); \
				(v_i) = 0; \
			} \
			M_NEG((v_i), (v_f)); \
		} else { \
			if ((n) < 32) { \
				(v_f) = ((v_f) >> (n)) | ((v_i) << \
				    (32 - (n))); \
				(v_i) = (v_i) >> (n); \
			} else { \
				(v_f) = (v_i) >> ((n) - 32); \
				(v_i) = 0; \
			} \
		 } \
	} while (0)

#define M_MPY(v_i, v_f, m)		/* v *= m */ \
	do { \
		register u_int32 a, b, c, d; \
		if ((v_i) < 0) { \
			M_NEG((v_i), (v_f)); \
			d = ((v_f) & 0xffff) * (m); \
			c = ((v_f) >> 16) * (m) + (d >> 16); \
			b = ((v_i) & 0xffff) * (m) + (c >> 16); \
			a = ((v_i) >> 16) * (m) + (b >> 16); \
			(v_i) = (a << 16) + (b & 0xffff); \
			(v_f) = (c << 16) + (d & 0xffff); \
			M_NEG((v_i), (v_f)); \
		} else { \
			d = ((v_f) & 0xffff) * (m); \
			c = ((v_f) >> 16) * (m) + (d >> 16); \
			b = ((v_i) & 0xffff) * (m) + (c >> 16); \
			a = ((v_i) >> 16) * (m) + (b >> 16); \
			(v_i) = (a << 16) + (b & 0xffff); \
			(v_f) = (c << 16) + (d & 0xffff); \
		} \
	} while (0)
/*
 * Operations - u,v are 64 bits; a,n are 32 bits.
 */
#define L_ADD(v, u)	M_ADD((v).l_i, (v).l_uf, (u).l_i, (u).l_uf)
#define L_SUB(v, u)	M_SUB((v).l_i, (v).l_uf, (u).l_i, (u).l_uf)
#define L_ADDHI(v, a)	M_ADD((v).l_i, (v).l_uf, (a), 0)
#define L_NEG(v)	M_NEG((v).l_i, (v).l_uf)
#define L_RSHIFT(v, n)	M_RSHIFT((v).l_i, (v).l_uf, n)
#define L_MPY(v, a)	M_MPY((v).l_i, (v).l_uf, a)
#define L_CLR(v)	((v).l_i = (v).l_uf = 0)
#define L_ISNEG(v)	((v).l_ui < 0)
#define L_LINT(v, a)			/* load integral part */ \
	do { \
		 (v).l_i = (a); \
		 (v).l_uf = 0; \
	} while (0)
#define L_GINT(v)	((v).l_i)	/* get integral part */

#else /* NTP_L64 */

/*
 * Single-precision macros for 64-bit machines
 *
 * A 64-bit fixed-point value is represented in 62-bit architectures as
 * a single 64-bit word, with the implied decimal point to the left of
 * bit 32.
 */
typedef long long l_fp;
#define L_ADD(v, u)	((v) += (u))
#define L_SUB(v, u)	((v) -= (u))
#define L_ADDHI(v, a)	((v) += (long long)(a) << 32)
#define L_NEG(v)	((v) = -(v))
#define L_RSHIFT(v, n) \
	do { \
		if ((v) < 0) \
			(v) = -(-(v) >> (n)); \
		else \
			(v) = (v) >> (n); \
	} while (0)
#define L_MPY(v, a)	((v) *= (a))
#define L_CLR(v)	((v) = 0)
#define L_ISNEG(v)	((v) < 0)
#define L_LINT(v, a)	((v) = (long long)(a) << 32)
#define L_GINT(v)	((v) < 0 ? -(-(v) >> 32) : (v) >> 32)

#endif /* NTP_L64 */
