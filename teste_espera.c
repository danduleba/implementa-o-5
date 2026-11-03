#include "sync_car.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum { PASSAGEIROS = 6 };

struct teste {
    struct station station;
    pthread_cond_t progresso;
    int retornaram;
};

static void exigir(int condicao, const char *mensagem)
{
    if (!condicao) {
        fprintf(stderr, "FAIL: %s\n", mensagem);
        exit(EXIT_FAILURE);
    }
}

static struct timespec prazo_ms(long milissegundos)
{
    struct timespec prazo;
    exigir(clock_gettime(CLOCK_REALTIME, &prazo) == 0, "consultar relogio");
    prazo.tv_nsec += milissegundos * 1000000L;
    prazo.tv_sec += prazo.tv_nsec / 1000000000L;
    prazo.tv_nsec %= 1000000000L;
    return prazo;
}

static void *passageiro(void *arg)
{
    struct teste *teste = arg;
    station_wait_for_car(&teste->station);
    pthread_mutex_lock(&teste->station.lock);
    teste->retornaram++;
    pthread_cond_signal(&teste->progresso);
    pthread_mutex_unlock(&teste->station.lock);
    return NULL;
}

/* As pausas permitem observar a chegada das threads sem espera ocupada. */
static void aguardar_estado(struct teste *teste, int esperando, int retornaram)
{
    pthread_mutex_lock(&teste->station.lock);
    for (int tentativa = 0; tentativa < 150; tentativa++) {
        if (teste->station.esperando == esperando &&
            teste->retornaram == retornaram) {
            pthread_mutex_unlock(&teste->station.lock);
            return;
        }
        struct timespec prazo = prazo_ms(20);
        int erro = pthread_cond_timedwait(&teste->progresso,
                                         &teste->station.lock, &prazo);
        exigir(erro == 0 || erro == ETIMEDOUT, "aguardar progresso");
    }
    exigir(0, "passageiros nao atingiram o estado esperado");
}

/* Verifica que uma notificacao sem vagas nao autoriza novas reservas. */
static void verificar_sem_vagas(struct teste *teste, int esperando,
                               int retornaram, int embarcando)
{
    pthread_mutex_lock(&teste->station.lock);
    pthread_cond_broadcast(&teste->station.carro_chegou);
    struct timespec prazo = prazo_ms(150);
    int erro;
    do {
        exigir(teste->station.esperando == esperando &&
               teste->retornaram == retornaram &&
               teste->station.vagas == 0 &&
               teste->station.embarcando == embarcando,
               "houve reserva sem vaga disponivel");
        erro = pthread_cond_timedwait(&teste->progresso,
                                     &teste->station.lock, &prazo);
        exigir(erro == 0 || erro == ETIMEDOUT, "verificar bloqueio");
    } while (erro != ETIMEDOUT);
    exigir(teste->station.esperando == esperando &&
           teste->retornaram == retornaram &&
           teste->station.vagas == 0 &&
           teste->station.embarcando == embarcando,
           "houve reserva sem vaga disponivel");
    pthread_mutex_unlock(&teste->station.lock);
}

int main(void)
{
    /* Limita a duracao caso uma implementacao incorreta trave o teste. */
    alarm(10);
    struct teste teste = {0};
    pthread_t threads[PASSAGEIROS];
    station_init(&teste.station);
    exigir(pthread_cond_init(&teste.progresso, NULL) == 0,
           "inicializar condicao do teste");

    for (int i = 0; i < PASSAGEIROS; i++) {
        exigir(pthread_create(&threads[i], NULL, passageiro, &teste) == 0,
               "criar passageiro");
    }

    aguardar_estado(&teste, PASSAGEIROS, 0);
    puts("PASS: 6 passageiros aguardam sem vagas.");
    verificar_sem_vagas(&teste, PASSAGEIROS, 0, 0);
    puts("PASS: aviso sem vagas mantem os passageiros esperando.");

    /* Simula a oferta de dois assentos pelo carro. */
    pthread_mutex_lock(&teste.station.lock);
    teste.station.vagas = 2;
    pthread_cond_broadcast(&teste.station.carro_chegou);
    pthread_mutex_unlock(&teste.station.lock);

    aguardar_estado(&teste, 4, 2);
    verificar_sem_vagas(&teste, 4, 2, 2);
    puts("PASS: 2 vagas liberam exatamente 2 passageiros; 4 continuam esperando.");
    puts("PASS: 2 reservas concluidas antes de qualquer confirmacao de embarque.");

    /* Simula as confirmacoes anteriores e a oferta do proximo carro. */
    pthread_mutex_lock(&teste.station.lock);
    teste.station.embarcando = 0;
    teste.station.vagas = 4;
    pthread_cond_broadcast(&teste.station.carro_chegou);
    pthread_mutex_unlock(&teste.station.lock);

    aguardar_estado(&teste, 0, PASSAGEIROS);
    for (int i = 0; i < PASSAGEIROS; i++) {
        exigir(pthread_join(threads[i], NULL) == 0, "finalizar passageiro");
    }
    exigir(teste.station.vagas == 0 && teste.station.embarcando == 4,
           "contadores finais incorretos");
    puts("PASS: as 4 vagas seguintes atendem todos os passageiros restantes.");

    pthread_cond_destroy(&teste.progresso);
    pthread_cond_destroy(&teste.station.carro_chegou);
    pthread_cond_destroy(&teste.station.embarque_concluido);
    pthread_mutex_destroy(&teste.station.lock);
    alarm(0);
    puts("Espera dos passageiros: PASS");
    return EXIT_SUCCESS;
}
