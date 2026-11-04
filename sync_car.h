#ifndef SYNC_CAR_H
#define SYNC_CAR_H

#include <pthread.h>

struct station {
    pthread_mutex_t trava;
    pthread_cond_t carro_chegou;
    pthread_cond_t embarque_concluido;

    int esperando;
    int vagas;
    int embarcando;
};

void station_init(struct station *estacao);
void station_load_car(struct station *estacao, int quantidade_vagas);
void station_wait_for_car(struct station *estacao);
void station_on_board(struct station *estacao);

#endif
