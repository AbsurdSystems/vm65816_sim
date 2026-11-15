
CC=gcc
CFLAGS=-lcurses -I.
OBJ= vm65816_sim.o wdc65c816.o circbuffer.o

all: vm65816_sim

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

vm65816_sim: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm $(OBJ) *.txt *.s vm65816_sim


