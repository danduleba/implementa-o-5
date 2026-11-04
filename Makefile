CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -pthread
REPETICOES ?= 200
LIMITE_PASSAGEIROS ?= 512

.PHONY: all testar-init testar-espera testar-limites estressar clean

all: sync_car

sync_car: sync_car.c sync_car.h sync_car-runner.c
	$(CC) $(CFLAGS) sync_car.c sync_car-runner.c -o sync_car

teste_init: sync_car.c sync_car.h teste_init.c
	$(CC) $(CFLAGS) sync_car.c teste_init.c -o teste_init

testar-init: teste_init
	./teste_init

teste_espera: sync_car.c sync_car.h teste_espera.c
	$(CC) $(CFLAGS) sync_car.c teste_espera.c -o teste_espera

testar-espera: teste_espera
	./teste_espera

teste_limites: sync_car.c sync_car.h teste_limites.c
	$(CC) $(CFLAGS) sync_car.c teste_limites.c -o teste_limites

testar-limites: teste_limites
	./teste_limites

estressar: teste_limites
	./teste_limites $(REPETICOES) $(LIMITE_PASSAGEIROS)

clean:
	rm -f sync_car sync_car.exe sync_car.o teste_init teste_init.exe teste_espera teste_espera.exe teste_limites teste_limites.exe
