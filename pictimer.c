#include <rtems.h>
#include <bsp.h>
#include <bsp/bspExt.h>
#include <bsp/irq.h>
#include <rtems/rtems_bsdnet.h>
#include <netinet/in.h>
#include <string.h>

#include <cexp.h>

#include "kern.h"
#include "rtemsdep.h"
#include "timex.h"
#include "pictimer.h"
#include "pcc.h"

#define TIMER_IVEC 					BSP_MISC_IRQ_LOWEST_OFFSET
#define TIMER_NO  					0	/* note that svgmWatchdog uses T3 */
#define TIMER_PRI 					6
#define UARG 						0

static unsigned long base_count;
static unsigned long timer_period_ns;


unsigned rtems_ntp_pictimer_irqs_missed = 0;

extern rtems_id rtems_ntp_ticker_id;

#ifndef USE_METHOD_B_FOR_DEMO
unsigned nano_ticks;
#endif

static void isr(void *arg)
{
#ifdef USE_METHOD_B_FOR_DEMO
	rtems_ntp_isr_snippet();
#else	
	nano_ticks = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Current_Count );
#endif

	if ( RTEMS_SUCCESSFUL != rtems_event_send(rtems_ntp_ticker_id, PICTIMER_SYNC_EVENT) )
		rtems_ntp_pictimer_irqs_missed++;
}

#ifndef USE_METHOD_B_FOR_DEMO
pcc_t
getPcc()
{
pcc_t cnt,tgl;
unsigned flags;

	/* disable timer ticks */
	flags = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority );
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority, flags | OPENPIC_MASK );


	cnt = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Current_Count );
	tgl = cnt ^ nano_ticks;

	/* reenable timer ticks */
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority, flags );

	cnt &= ~ OPENPIC_TIMER_TOGGLE;
	if ( tgl & OPENPIC_TIMER_TOGGLE ) {
		/* timer rolled over but ISR hadn't had a chance to
		 * execute ntp_tick_adjust()
		 */
		cnt-=base_count;
	}
	/* OPENPIC timer runs backwards */
	cnt = base_count - cnt;

	return cnt;
}

pcc_t
setPccBase()
{
	return base_count;
}
#endif

static int timerConnected = 0;

int pictimerInstall()
{
unsigned              tmp;

	if ( timerConnected ) {
		fprintf(stderr,"OpenPIC Timer #%i already connected\n", TIMER_NO);
		return -1;
	}

#if 0 /* just use compile-time constants for now */
	if ( instance > 3 ) {
		fprintf(stderr,"Invalid timer instance %i; (0..3 allowed)\n",
				instance);
		return -1;
	}

	if ( miscvec >= BSP_MISC_IRQ_NUMBER ) {
		fprintf(stderr,"Invalid Openpic MISC vector number %i; (0..%i allowed)\n",
				miscvec, BSP_MISC_IRQ_NUMBER-1);
		return -1;
	}

	timer_intvec = BSP_MISC_IRQ_LOWEST_OFFSET + miscvec;
#endif

	printf("Using OpenPIC Timer #%i at %p\n", TIMER_NO, &OpenPIC->Global.Timer[TIMER_NO]);

	base_count = (tmp = in_le32( &OpenPIC->Global.Timer_Frequency )) / TIMER_FREQ;
	timer_period_ns = NANOSECOND / tmp;
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Base_Count, OPENPIC_MASK | base_count );
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Base_Count, base_count );
	/* map to 1st CPU */
	openpic_maptimer(TIMER_NO, 1);
	if ( 0 == bspExtInstallSharedISR( TIMER_IVEC, isr, UARG, BSPEXT_ISR_NONSHARED ) ) {
		openpic_inittimer( TIMER_NO, TIMER_PRI, TIMER_IVEC );
		timerConnected = 1;
		return 0;
	}
	fprintf(stderr,"Unable to install shared ISR for OpenPIC Timer #%i\n",TIMER_NO);
	return -1;
}

int
pictimerCleanup()
{
	if ( !timerConnected )
		return 0;
	openpic_inittimer( TIMER_NO, 0, 0 );
	openpic_maptimer( TIMER_NO, 0 );
	
	if ( bspExtRemoveSharedISR(TIMER_IVEC, isr, UARG) ) {
		fprintf(stderr,"Unable to remove shared ISR for OpenPIC Timer #%i\n",TIMER_NO);
		return -1;
	}
	timerConnected = 0;
	return 0;
}

unsigned
pictimerEnable(int v)
{
unsigned tmp;
	tmp = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority );
	if ( v )
		out_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority, tmp & ~OPENPIC_MASK );
	return tmp;
}

unsigned
pictimerDisable()
{
unsigned tmp;
	tmp = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority );
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority, tmp | OPENPIC_MASK );
	return !(tmp | OPENPIC_MASK);
}
