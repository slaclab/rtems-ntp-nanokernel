#ifndef RTEMS_NTP_PICTIMER_SUPPORT_H
#define RTEMS_NTP_PICTIMER_SUPPORT_H

/* Notes: - freq<0 is legal, instructing the driver to use -freq
 *          directly as a period in timer-clicks. Otherwise, the
 *          period is set to pic_timer_frequency / freq
 *        - timer_no (0..3) is passed to the ISR as and argument.
 * RETURNS: 0 on success.
 */
int pictimerInstall(unsigned timer_no, int irqPriority, int freq, void (*isr)(void*));

/* install nanoclock ISR handler on 'timer_no'
 * (wrapper to pictimerInstall())
 * RETURNS 0 on success.
 */
int pictimerInstallClock(unsigned timer_no);

/* disable interrupts and remove ISR installed to timer_no
 */
int pictimerCleanup(unsigned timer_no);

/* enable timer interrupts at timer (if v!=0); returns
 * current value (status *prior* to clearing the mask)
 * of vector_priority. If v==0, just the vec_pri register
 * contents are returned without actually enabling interrupts
 */
unsigned pictimerEnable(unsigned timer_no, int v);

/* disable interrupts at timer.
 * RETURN old status info (nonzero if irqs were
 *        enabled originally)
 */
unsigned pictimerDisable(unsigned timer_no);

#define PICTIMER_SYNC_EVENT	RTEMS_EVENT_2

#endif
