#ifndef NTP_PCC_HEADER_H
#define NTP_PCC_HEADER_H

#define PCC_WIDTH 32

typedef long pcc_t;

#ifdef __PPC__
static inline pcc_t rpcc()
{
long pcc;
	asm volatile ("mftb %0":"=r"(pcc));
	return pcc;
}
#else
extern pcc_t rpcc();
#endif

#endif
