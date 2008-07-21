PRODUCT = gluff
VERSION = 1.0

SHELL = /bin/sh
top_srcdir = .
srcdir = .

.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
DEFINES = -DHAVE_CONFIG_H
CFLAGS = -I. -g -O2 -I/usr/local/include -Wall $(DEFINES)
LDFLAGS =  -L/usr/local/lib
LIBS = -lmysqlclient -lsqlite3 
INSTALL = /usr/bin/install -c
prefix = /opt/gluff
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
mandir = ${datarootdir}/man
sysconfdir = ${prefix}/etc
datarootdir = ${prefix}/share

DISTFILES =

TARGET1=gluff
SOURCES1=gluff.c
OBJS1=gluff.o

TARGETS=$(TARGET1) 
SOURCES=$(SOURCES1)
OBJS=$(OBJS1)

all: $(TARGETS)

install: all
	$(top_srcdir)/mkinstalldirs $(bindir)
	$(INSTALL) $(TARGET1) $(bindir)/

$(TARGET1): $(OBJS1)
	$(CC) $(CFLAGS) -o $(TARGET1) $(OBJS1) $(LDFLAGS) $(LIBS)

$(OBJS1): $(SOURCES1)

clean:
	/bin/rm -f $(TARGETS) *.o core

distclean: clean config-clean

config-clean: confclean-recursive

confclean-recursive: cfg-clean

cfg-clean:
	/bin/rm -f Makefile config.h config.status config.cache config.log

mostlyclean: clean

maintainer-clean: clean

# automatic re-running of configure if the configure.in file has changed
${srcdir}/configure: configure.in 
	cd ${srcdir} && autoconf

# autoheader might not change config.h.in, so touch a stamp file
${srcdir}/config.h.in: stamp-h.in
${srcdir}/stamp-h.in: configure.in 
		cd ${srcdir} && autoheader
		echo timestamp > ${srcdir}/stamp-h.in

config.h: stamp-h
stamp-h: config.h.in config.status
	./config.status
Makefile: Makefile.in config.status
	./config.status
config.status: configure
	./config.status --recheck



