#ifndef NTP_PCC_HEADER_H
#define NTP_PCC_HEADER_H

/* there might be mild dependencies on sizeof(pcc_t) being >= sizeof(long) */
#define PCC_WIDTH 32
typedef unsigned long pcc_t;

/* There are two methods for implementing nanosecond
 * clock resolution:
 *
 * A) the timer responsible for generating system clock
 *    interrupts itself is readable with a high resolution and
 *    its value at the time of interrupt generation is known.
 *    This is the preferred method.
 *
 * B) system clock ISR reads a PCC ("processor cycle counter")
 *    which is possibly asynchronous to the system clock.
 *    At each interrupt, the ISR saves the PCC to a global variable.
 *    It is essential to minimize jitter of this reading with
 *    respect to interrupt time. Therefore, reading PCC should
 *    be performed ASAP.
 *
 */

#ifdef __PPC__

#ifdef USE_METHOD_B_FOR_DEMO

/* we use the free running timebase register as a PCC
 * (which is actually also locked to the decrementer but
 * this is for demo purposes only)
 */
static volatile pcc_t pcc_base;
static volatile pcc_t lastIrqTime;

static inline pcc_t getPcc()
{
unsigned pcc;
	asm volatile("mftb %0":"=r"(pcc));
	return pcc - pcc_base;
}

/* The clock driver should call this directly from the ISR
 * for demo purposes we use a timer, however.
 */
static inline void rtems_ntp_isr_snippet()
{
	asm volatile("mftb %0":"=r"(lastIrqTime));
}

static inline pcc_t setPccBase()
{
pcc_t oldbase = pcc_base;
	pcc_base = lastIrqTime;
	return pcc_base - oldbase;
}

#elif !defined(USE_PICTIMER) /* ifdef method_B */

/* PowerPC 'Method A' implementation -- use for production */

/* this routine returns the difference between the current PCC
 * and the PCC base time (==IRQ time).
 * In Method A, we know that the base time is actually always 0.
 * However, we allow the clock adjustment task to run at a subharmonic
 * of the system clock. Therefore, we must account for the number
 * of ticks expired since the (subharmonic) time was updated.
 *
 * --> subharmonic task calls setPccBase() and remembers the current
 *     system clock tick as the 'base'
 * --> getPcc reads the PCC and adds (ticks-base) * clock_period
 */

static rtems_unsigned32 tick_base;
extern unsigned Clock_Decrementer_value;

static inline pcc_t getPcc()
{
unsigned        flags;
unsigned        pcc;
unsigned		rtemsTicks;

	/* reading the PCC (the decrementer register) and
	 * the current system tick counter must be
	 * atomical...
	 */
	rtems_interrupt_disable( flags );
	PPC_Get_decrementer(pcc);
	rtemsTicks   = Clock_driver_ticks;
	rtems_interrupt_enable( flags );

	/* even correct if the decrementer has underflown */
	pcc = Clock_Decrementer_value - pcc;

	/* account for the number of ticks expired since setPccBase()
	 * was called for the last time
	 */
	rtemsTicks = rtemsTicks - tick_base;

	return pcc + Clock_Decrementer_value * rtemsTicks;
}

static inline pcc_t setPccBase()
{
unsigned oldbase = tick_base;
	tick_base = Clock_driver_ticks;
	return Clock_Decrementer_value * (tick_base - oldbase);
}

#else /* if method_B elif !pictimer */
/* pictimer.c provides an implementation for method A */
extern pcc_t getPcc();
extern pcc_t setPccBase();
#endif

#else	/* ifdef __PPC__ */
extern pcc_t getPcc();
extern pcc_t setPccBase();
#endif

#endif
