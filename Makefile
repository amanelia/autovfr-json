CC = gcc
CFLAGS += -g -Wall
LDFLAGS += -g -Wall
LIBS += -ljson-c

OBJS = autovfr.o

PROGRAM = autovfr

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f autovfr $(OBJS)
