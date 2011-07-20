# Makefile for envv.

VERSION=1.7

SRCS=envv.c
FILES=$(SRCS) Makefile README envv.1

#Which compiler to use?
CC=gcc

all: envv

envv: envv.c
	$(CC) -o envv $(CFLAGS) $(CDEFS) $(CEXTRAS) envv.c

clean:
	rm -f *~ *.o core

clobber:
	rm -f *~ *.o core envv

targz:
	git archive --format=tar --prefix=envv-$(VERSION)/ HEAD | gzip -v -9 > envv-$(VERSION).tar.gz
