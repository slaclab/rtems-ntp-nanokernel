#
# Makefile for kern
#
PROGRAM= kern
COMPILER= gcc
COPTS= -Wall
BINDIR= /usr/local/bin
INSTALL= install
DEFS=
#
INCL= -I../include
CFLAGS= $(COPTS) $(DEFS) $(INCL)
CC= $(COMPILER)
LIB= -lm
#
SOURCE= kern.c ktime.c micro.c gauss.c
OBJS= kern.o ktime.o micro.o gauss.o
EXEC= kern

all:	$(PROGRAM)

kern:	$(OBJS)
	$(CC) $(COPTS) -o $@ $(OBJS) $(LIB)

install: $(BINDIR)/$(PROGRAM)

$(BINDIR)/$(PROGRAM): $(PROGRAM)
	$(INSTALL) -c -m 0755 $(PROGRAM) $(BINDIR)

tags:
	ctags *.c *.h

depend:
	mkdep $(CFLAGS) $(SOURCE)

clean:
	-@rm -f $(PROGRAM) $(EXEC) $(OBJS)
