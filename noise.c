/*
 * Program to generate data file for simulated random-walk frequency
 * noise. It does this by generating random Gaussian samples and
 * integrating once for frequency and twice for phase. The file is read
 * later by Matlab to generate Allan deviation plots and time series
 * plots.
 *
 * Output format: <second> <phase> <frequency>
 * ...
 * 10 -0.319 -0.064
 * 11 -0.370 -0.051
 * 12 -0.412 -0.04
 * ...
 */
#include <stdlib.h>

extern double gauss(double);
double phase, freq;
double sigma = 4.5e-10;			/* default matches ntpsim */
long sim_end = 20;			/* default for test */

int
main(
	int argc,
	char **argcv
	)
{
	int i, temp;

	while ((temp = getopt(argc, argcv, "f:s:")) != -1) {
		switch (temp) {

			/*
			 * -f set frequency parameter
			 */
			case 'f':
			sscanf(optarg, "%lf", &sigma);
			continue;

			/*
			 * -s set run length (s)
			 */
			case 's':
			sscanf(optarg, "%ld", &sim_end);
			continue;

			/*
			 * unknown command line switch
			 */
			default:
			printf("unknown switch %s\n", optarg);
			continue;
		}
	} 
	for (i = 0; i < sim_end; i++) {
		freq += gauss(sigma);
		phase += freq;
		printf("%d %.3f %.3f\n", i, phase * 1e6, freq * 1e6);
	}
}
