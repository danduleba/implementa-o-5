#include "sync_car.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    struct station estacao;
    station_init(&estacao);

    if (estacao.esperando != 0 || estacao.vagas != 0 ||
        estacao.embarcando != 0) {
        fprintf(stderr, "Falha: os contadores devem iniciar em zero.\n");
        return EXIT_FAILURE;
    }

    if (pthread_mutex_lock(&estacao.trava) != 0) {
        fprintf(stderr, "Falha ao adquirir o mutex.\n");
        return EXIT_FAILURE;
    }

    if (pthread_mutex_unlock(&estacao.trava) != 0) {
        fprintf(stderr, "Falha ao liberar o mutex.\n");
        return EXIT_FAILURE;
    }

    pthread_cond_destroy(&estacao.carro_chegou);
    pthread_cond_destroy(&estacao.embarque_concluido);
    pthread_mutex_destroy(&estacao.trava);

    printf("Inicializacao da estacao: PASS\n");
    return EXIT_SUCCESS;
}
