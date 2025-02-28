#
#  Makefile.leaf,v 1.7 2002/07/22 22:56:09 joel Exp
#
# Templates/Makefile.leaf
# 	Template leaf node Makefile
#

USE_PICTIMER=NO
USE_METHOD_B=NO
# use pentium tsc register as a high resolution counter
# Enabling this on any arch other than x86 has no effect.
# DO NOT use this on a  x86 CPU < pentium or ntpclock will
# crash!
USE_RDTSC=YES

# C source names, if any, go here -- minus the .c
C_PIECES=ktime rtemsdep $(C_PIECES_USE_PICTIMER_$(USE_PICTIMER))
C_FILES=$(C_PIECES:%=%.c)
C_O_FILES=$(C_PIECES:%=${ARCH}/%.o)

C_PIECES_USE_PICTIMER_YES=pictimer
C_PIECES_USE_PICTIMER_NO=
DEFINES_USE_PICTIMER_YES=-DUSE_PICTIMER
DEFINES_USE_METHOD_B_YES=-DUSE_METHOD_B_FOR_DEMO
DEFINES_USE_RDTSC_YES=-DUSE_RDTSC

# C++ source names, if any, go here -- minus the .cc
CC_PIECES=
CC_FILES=$(CC_PIECES:%=%.cc)
CC_O_FILES=$(CC_PIECES:%=${ARCH}/%.o)

H_FILES=
INST_HEADERS=timex.h

# Assembly source names, if any, go here -- minus the .S
S_PIECES=
S_FILES=$(S_PIECES:%=%.S)
S_O_FILES=$(S_FILES:%.S=${ARCH}/%.o)

SRCS=$(C_FILES) $(CC_FILES) $(H_FILES) $(S_FILES)
OBJS=$(C_O_FILES) $(CC_O_FILES) $(S_O_FILES)

# If your PGMS target has the '.exe' extension, a statically
# linked application is generated.
# If it has a '.obj' extension, a loadable module is built.

PGMS=${ARCH}/ntpclock.obj

#  List of RTEMS Classic API Managers to be included in the application
#  goes here. Use:
#     MANAGERS=all
# to include all RTEMS Classic API Managers in the application or
# something like this to include a specific set of managers.
#     MANAGERS=io event message rate_monotonic semaphore timer
#
# UNUSED for loadable modules
MANAGERS=XXX

include $(RTEMS_MAKEFILE_PATH)/Makefile.inc

include $(RTEMS_CUSTOM)
include $(RTEMS_ROOT)/make/leaf.cfg

#
# (OPTIONAL) Add local stuff here using +=
#

DEFINES  += $(DEFINES_USE_PICTIMER_$(USE_PICTIMER))
DEFINES  += $(DEFINES_USE_METHOD_B_$(USE_METHOD_B))
DEFINES  += $(DEFINES_USE_RDTSC_$(USE_RDTSC))
CPPFLAGS +=
CFLAGS   +=

#
# CFLAGS_DEBUG_V are used when the `make debug' target is built.
# To link your application with the non-optimized RTEMS routines,
# uncomment the following line:
# CFLAGS_DEBUG_V += -qrtems_debug
#

#LD_PATHS  += xxx-your-EXTRA-library-paths-go-here, if any
#LD_LIBS   += xxx-your-libraries-go-here eg: -lvx
LDFLAGS   +=

#
# Add your list of files to delete here.  The config files
#  already know how to delete some stuff, so you may want
#  to just run 'make clean' first to see what gets missed.
#  'make clobber' already includes 'make clean'
#

#CLEAN_ADDITIONS += xxx-your-debris-goes-here
CLOBBER_ADDITIONS +=

all:	${ARCH} $(SRCS) $(PGMS)

#How to make a relocatable object
$(filter %.obj, $(PGMS)): ${OBJS}
	$(make-obj)

#How to make an executable (statically linked)
$(filter %.exe,$(PGMS)): ${LINK_FILES}
	$(make-exe)
ifdef ELFEXT
ifdef XSYMS
	$(XSYMS) $(@:%.exe=%.$(ELFEXT)) $(@:%.exe=%.sym)
endif
endif

ifndef RTEMS_SITE_INSTALLDIR
RTEMS_SITE_INSTALLDIR = $(PROJECT_RELEASE)
endif

${RTEMS_SITE_INSTALLDIR}/include/sys \
${RTEMS_SITE_INSTALLDIR}/include \
${RTEMS_SITE_INSTALLDIR}/lib \
${RTEMS_SITE_INSTALLDIR}/bin:
	test -d $@ || mkdir -p $@

# Install the program(s), appending _g or _p as appropriate.
# for include files, just use $(INSTALL_CHANGE)
#
install:  all $(RTEMS_SITE_INSTALLDIR)/bin ${RTEMS_SITE_INSTALLDIR}/include/sys
	$(INSTALL_VARIANT) -m 555 ${PGMS} ${PGMS:%.exe=%.bin} ${PGMS:%.exe=%.sym} ${RTEMS_SITE_INSTALLDIR}/bin
	$(INSTALL_CHANGE) -m 444 ${INST_HEADERS} ${RTEMS_SITE_INSTALLDIR}/include/sys
