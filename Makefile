CFLAGS=-g -I/usr/local/include
LDFLAGS=-levent_core -L/usr/local/lib/event2 -static
all: pmsrv pmbind pmresolve

pmsrv: pmsrv.o portmap.o pmsrv_run.o
	$(CC) $(LDFLAGS) -o pmsrv pmsrv.o portmap.o pmsrv_run.o
pmbind: pmbind.o portmap.o
	$(CC) $(LDFLAGS) -o pmbind pmbind.o portmap.o
pmresolve: pmresolve.o portmap.o
	$(CC) $(LDFLAGS) -o pmresolve pmresolve.o portmap.o


