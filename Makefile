CFLAGS=-g
LDFLAGS=-levent
all: portmap assign resolve

portmap: portmap.o
	$(CC) $(LDFLAGS) -o portmap portmap.o
assign: assign.o
	$(CC) $(LDFLAGS) -o assign assign.o
resolve: resolve.o
	$(CC) $(LDFLAGS) -o resolve resolve.o


