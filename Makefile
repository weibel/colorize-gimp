CC=gcc

GIMPTOOL=gimptool-2.0

GIMP_LDFLAGS=`$(GIMPTOOL) --libs`
GIMP_CFLAGS=`$(GIMPTOOL) --cflags`

UMFPACK_CFLAGS=-I/usr/include/suitesparse -I/usr/include/umfpack
UMFPACK_LDFLAGS=-lf77blas -lumfpack -lamd -lcholmod -lm

CFLAGS=-g -Wall -O3 $(GIMP_CFLAGS) $(UMFPACK_CFLAGS)
LDFLAGS=$(GIMP_LDFLAGS) $(UMFPACK_LDFLAGS)

all: colorize

install: colorize
	$(GIMPTOOL) --install-bin colorize

colorize: colorize.o colorize-plugin.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	-rm -f *~ *.o core colorize
