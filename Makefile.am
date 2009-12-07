#  $Id$
AUTOMAKE_OPTIONS=foreign

include $(top_srcdir)/rtems-pre.am

EXEEXT=$(OBJEXEEXT)

ntpclock_SOURCES      = ktime.c rtemsdep.c
ntpclock_SOURCES     += high.h kern.h l_fp.h pcc.h pcc-host.h pictimer.h
ntpclock_SOURCES     += high.h kern.h l_fp.h pcc.h pcc-host.h pictimer.h
ntpclock_SOURCES     += rtemsdep.h tpro.h
ntpclock_CPPFLAGS     = -DUSE_RDTSC

#EXTRA_DIST=pictimer.c
#EXTRA_DIST+=gauss.c hightime.c jitter.c kern.c micro.c noise.c profile.c tprotime.c

include_sys_HEADERS   = timex.h

bin_PROGRAMS          = ntpclock

ntpclock_LINK         = $(OBJLINK)

include $(top_srcdir)/rtems.am

