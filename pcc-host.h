#ifndef NTP_PCC_HEADER_H
#define NTP_PCC_HEADER_H

#include <stdint.h>

/* there might be mild dependencies on sizeof(pcc_t) being >= sizeof(long) */

#define PCC_WIDTH 32
typedef uint32_t pcc_t;

long long
rpcc();

void
microset_from_saved(pcc_t saved_pcc, struct timespec *pt);

#endif
