#MOTIFLIBS = -lSgm -lXm -lXt -lXpm -lX11 -lPW
MOTIFLIBS = -lXm -lXt -lX11 -lPW
LLDLIBS=  $(MOTIFLIBS) 

CC=c99
CFLAGS=-mips3 -n32 -g 

COBJECTS=\
	sgirc.o \
	ircclient.o \
	messagetarget.o \
	message.o \
	prefs.o \
	thirdparty/cJSON.o \
	$(NULL)

TARGETS=	\
	sgirc\
	$(NULL)

default all: $(TARGETS)

clean:
	rm -f $(TARGETS)
	rm -f $(COBJECTS)

.SUFFIXES: .o .c

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

sgirc: $(COBJECTS)
	$(CC) $(CFLAGS) $(COBJECTS) $(LLDLIBS) -o sgirc
#/usr/bin/CC         -I/usr/Motif-1.2/include    -nostdinc -I/usr/include/CC -I/usr/include -mips3 -n32 -g  -MDupdate Makedepend sgirc.c++  -L/usr/Motif-1.2/lib32 -mips3 -n32 -quickstart_info -nostdlib -L/usr/lib32/mips3 -L/usr/lib32 -L/usr/lib32/internal   -lvk -lSgm -lXm -lXt -lX11 -lPW -lm   -o sgirc

