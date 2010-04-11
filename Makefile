# Makefile for envv.

VERSION=1.6

SRCS=envv.c
FILES=$(SRCS) Makefile README envv.1

#Which compiler to use?
CC=gcc

all: envv

envv: envv.c
	$(CC) -o envv $(CFLAGS) $(CDEFS) $(CEXTRAS) envv.c

clean:
	rm -f *~ *.o core envv

clobber:
	rm -f *~ *.o core envv

tarZ:
	tar cvf envv-$(VERSION).tar $(FILES)
	compress -v envv-$(VERSION).tar

shar:
	shar "-nenvv-$(VERSION)" -l45 -o./Shar $(FILES)
