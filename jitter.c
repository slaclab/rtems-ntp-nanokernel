/*
 * This program can be used to calibrate the clock reading jitter of a
 * particular CPU and operating system. It first tickles every element
 * of an array, in order to force pages into memory, then repeatedly calls
 * gettimeofday() and, finally, writes out the time values for later
 * analysis. From this you can determine the jitter and if the clock ever
 * runs backwards.
 */
#include <sys/time.h>
#include <stdio.h>
#include <sys/timex.h>

#define NBUF 20002
#define ntp_gettime(t)  syscall(SYS_ntp_gettime, (t))
#define ntp_adjtime(t)  syscall(SYS_ntp_adjtime, (t))


int
main(
	int argc,
	char *argv[]
	)
{
	struct timeval ts, tr;
	struct ntptimeval ntv;
	long temp, j, i, gtod[NBUF];

	temp = ntp_gettime(&ntv);
	ts = ntv.time;
	if (temp < 0) {
		printf("NTP precision time kernel not available\n");
		exit(0);
	}

	/*
	 * Force pages into memory
	 */
	for (i = 0; i < NBUF; i ++)
		gtod[i] = 0;

	/*
	 * Construct gtod array
	 */
	for (i = 0; i < NBUF; i ++) {
		(void)ntp_gettime(&ntv);
/*
printf("%08lx %08lx\n", ntv.time.tv_sec, ntv.time.tv_usec);
*/
		tr = ntv.time;
		gtod[i] = (tr.tv_sec - ts.tv_sec) * 1000000000 + tr.tv_usec;
	}

	/*
	 * Write out gtod array for later processing with S
	 */
	for (i = 0; i < NBUF - 2; i++) {
/*
		  printf("%lu\n", gtod[i]);
*/		
		gtod[i] = gtod[i + 1] - gtod[i];
		printf("%lu\n", gtod[i]);
	}

	/*
	 * Sort the gtod array and display deciles
	 */
	for (i = 0; i < NBUF - 2; i++) {
		for (j = 0; j <= i; j++) {
			if (gtod[j] > gtod[i]) {
				temp = gtod[j];
				gtod[j] = gtod[i];
				gtod[i] = temp;
			}
		}
	}
	fprintf(stderr, "First rank\n");
	for (i = 0; i < 10; i++)
	    fprintf(stderr, "%10ld%10ld\n", i, gtod[i]);
	fprintf(stderr, "Last rank\n");
	for (i = NBUF - 12; i < NBUF - 2; i++)
	    fprintf(stderr, "%10ld%10ld\n", i, gtod[i]);
	exit(0);
}
