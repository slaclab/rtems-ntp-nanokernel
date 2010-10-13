#  $Id$
AUTOMAKE_OPTIONS=foreign

include $(top_srcdir)/rtems-pre.am

EXEEXT=$(OBJEXEEXT)

ntpclock_SOURCES      = ktime.c rtemsdep.c
ntpclock_SOURCES     += high.h kern.h l_fp.h pcc.h pcc-host.h pictimer.h
ntpclock_SOURCES     += high.h kern.h l_fp.h pcc.h pcc-host.h pictimer.h
ntpclock_SOURCES     += rtemsdep.h tpro.h
ntpclock_CPPFLAGS     = -DUSE_RDTSC

include_sys_HEADERS   = timex.h

bin_PROGRAMS          = ntpclock

ntpclock_LINK         = $(OBJLINK)

HOSTPROG              = rtemssim

EXTRA_PROGRAMS        = $(HOSTPROG)

if ENABLE_DIAG_HOSTPROGS
# ugly hack to avoid using $(EXEEXT)
# we MUST let 'configure' substitute something
# the HOSTPROG substitution evaluates to
# $(HOSTPROG)$(HOSTEXEEXT)
exechostbin_PROGRAMS  = @HOSTPROGRAM@
endif

rtemssim_SOURCES      = rtemssim.c ktime.host.c
rtemssim_LDADD        = -lm

rtemssim.$(OBJEXT) %.host.$(OBJEXT):CC=$(HOSTCC)

%.host.c: %.c
	$(RM) $@
	ln -s $^ $@

CLEANFILES = *.host.c

#rtemssim.$(OBJEXT): rtemssim.c rtemsdep.c
#	$(HOSTCC) -c -O2 -o $@ -I. -I.. -I$(top_builddir) $<

EXTRA_DIST  = pictimer.c
EXTRA_DIST += gauss.c hightime.c jitter.c kern.c micro.c noise.c profile.c tprotime.c
EXTRA_DIST += test.sh kern.sh noise.sh
EXTRA_DIST += html/util.htm html/theory.htm html/api.htm html/descrip.htm
EXTRA_DIST += html/index.htm html/proof.htm
EXTRA_DIST += html/scripts/header.txt html/scripts/style.css
EXTRA_DIST += html/scripts/footer.txt html/implem.htm html/changes.htm
EXTRA_DIST += html/pic/pps_p.gif html/pic/grundoon_51397_f.gif html/pic/loop1.gif
EXTRA_DIST += html/pic/pllfll.gif html/pic/phk.gif html/pic/pps_fp.gif html/pic/DM41_tp.gif
EXTRA_DIST += html/pic/allan.gif html/pic/churchy_51397_t.gif html/pic/synth_p.gif
EXTRA_DIST += html/pic/churchy_51397_f.gif html/pic/synth1_tp.gif html/pic/synth1_fp.gif
EXTRA_DIST += html/pic/DM4_tp.gif html/pic/DM41_fp.gif html/pic/synth_tp.gif html/pic/pps1_tp.gif
EXTRA_DIST += html/pic/DM4_p.gif html/pic/predict.gif html/pic/gtod1.gif html/pic/pps_tp.gif
EXTRA_DIST += html/pic/pps.gif html/pic/loop.gif html/pic/grundoon_51397_t.gif html/pic/pps1_fp.gif
EXTRA_DIST += html/pic/DM4_fp.gif html/pic/gtod2.gif html/pic/DM30_tg.gif html/pic/tonea.gif
EXTRA_DIST += html/pic/synth_fp.gif html/pic/home.gif html/pic/DM40_tg.gif

EXTRA_DIST += html/icons/home.gif html/icons/mail2.gif
EXTRA_DIST += freebsd/timex.h freebsd/timepps.h
EXTRA_DIST += sunos/timex.h sunos/ppsapi.c sunos/timepps.h
EXTRA_DIST += alpha/timex.h alpha/ppsapi.c alpha/timepps.h
EXTRA_DIST += solaris/timex.h solaris/timepps.h

include $(top_srcdir)/rtems.am
