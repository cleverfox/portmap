CFLAGS=-g
LDFLAGS=-levent
all: portmap assign

portmap: portmap.o
	$(CC) $(LDFLAGS) -o portmap portmap.o
assign: assign.o
	$(CC) $(LDFLAGS) -o assign assign.o

