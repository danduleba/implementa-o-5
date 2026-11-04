#include "sync_car.h"
#include <stdio.h>
#include <stdlib.h>

void station_init(struct station *estacao)
{
    if (pthread_mutex_init(&estacao->trava, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar o mutex.\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&estacao->carro_chegou, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar a condicao do carro.\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&estacao->embarque_concluido, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar a condicao do embarque.\n");
        exit(EXIT_FAILURE);
    }

    estacao->esperando = 0;
    estacao->vagas = 0;
    estacao->embarcando = 0;
}

void station_wait_for_car(struct station *estacao)
{
    pthread_mutex_lock(&estacao->trava);
    estacao->esperando++;
    while (estacao->vagas == 0) {
        pthread_cond_wait(&estacao->carro_chegou, &estacao->trava);
    }
    estacao->vagas--;
    estacao->esperando--;
    estacao->embarcando++;
    pthread_mutex_unlock(&estacao->trava);
}
void station_load_car(struct station *estacao, int quantidade_vagas)
{
    pthread_mutex_lock(&estacao->trava);

    estacao->vagas = quantidade_vagas;

    pthread_cond_broadcast(&estacao->carro_chegou);

    while (estacao->embarcando > 0 ||
           (estacao->vagas > 0 && estacao->esperando > 0)) {
        pthread_cond_wait(&estacao->embarque_concluido,&estacao->trava);
    }

    estacao->vagas = 0;

    pthread_mutex_unlock(&estacao->trava);
}

void station_on_board(struct station *estacao)
{
    pthread_mutex_lock(&estacao->trava);

    estacao->embarcando--;

    if (estacao->embarcando == 0) {
        pthread_cond_signal(&estacao->embarque_concluido);
    }

    pthread_mutex_unlock(&estacao->trava);
}