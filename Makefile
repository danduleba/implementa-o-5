CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -pthread

.PHONY: all testar-init clean

all: sync_car.o

sync_car.o: sync_car.c sync_car.h
	$(CC) $(CFLAGS) -c sync_car.c -o sync_car.o

teste_init: sync_car.c sync_car.h teste_init.c
	$(CC) $(CFLAGS) sync_car.c teste_init.c -o teste_init

testar-init: teste_init
	./teste_init

clean:
	rm -f sync_car.o teste_init teste_init.exe
