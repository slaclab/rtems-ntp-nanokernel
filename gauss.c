/*
 * Generate a random number plucked from a Gaussian distribution with
 * mean zero and specified standard deviation. Needs cos(), log(),
 * rand48(), sqrt() from math library.
 */
#include <stdlib.h>
#include <math.h>

double gauss(
	double sigma		/* standard deviation */
	)
{
	double q1, q2;		/* double temps */

	while ((q1 = drand48()) == 0);
	q2 = drand48();
/*
printf("sigma %10.5lf q1 %10.5lf q2 %10.5lf\n", sigma, q1, q2);
*/
	return (sigma * sqrt(-2 * log(q1)) * cos(2 * M_PI * q2));
}
