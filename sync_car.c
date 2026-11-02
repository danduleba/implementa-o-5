#include "sync_car.h"
#include <stdio.h>
#include <stdlib.h>

void station_init(struct station *station)
{
    if (pthread_mutex_init(&station->lock, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar o mutex.\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&station->carro_chegou, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar a condicao do carro.\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&station->embarque_concluido, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar a condicao do embarque.\n");
        exit(EXIT_FAILURE);
    }

    station->esperando = 0;
    station->vagas = 0;
    station->embarcando = 0;
}
