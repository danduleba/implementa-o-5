#include "sync_car.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum { PASSAGEIROS = 6 };

struct teste {
    struct station estacao;
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

static void *passageiro(void *argumento)
{
    struct teste *teste = argumento;
    station_wait_for_car(&teste->estacao);
    pthread_mutex_lock(&teste->estacao.trava);
    teste->retornaram++;
    pthread_cond_signal(&teste->progresso);
    pthread_mutex_unlock(&teste->estacao.trava);
    return NULL;
}

/* As pausas permitem observar a chegada dos passageiros sem espera ocupada. */
static void aguardar_estado(struct teste *teste, int esperando, int retornaram)
{
    pthread_mutex_lock(&teste->estacao.trava);
    for (int tentativa = 0; tentativa < 150; tentativa++) {
        if (teste->estacao.esperando == esperando &&
            teste->retornaram == retornaram) {
            pthread_mutex_unlock(&teste->estacao.trava);
            return;
        }
        struct timespec prazo = prazo_ms(20);
        int erro = pthread_cond_timedwait(&teste->progresso,
                                         &teste->estacao.trava, &prazo);
        exigir(erro == 0 || erro == ETIMEDOUT, "aguardar progresso");
    }
    exigir(0, "passageiros nao atingiram o estado esperado");
}

/* Verifica que uma notificacao sem vagas nao autoriza novas reservas. */
static void verificar_sem_vagas(struct teste *teste, int esperando,
                               int retornaram, int embarcando)
{
    pthread_mutex_lock(&teste->estacao.trava);
    pthread_cond_broadcast(&teste->estacao.carro_chegou);
    struct timespec prazo = prazo_ms(150);
    int erro;
    do {
        exigir(teste->estacao.esperando == esperando &&
               teste->retornaram == retornaram &&
               teste->estacao.vagas == 0 &&
               teste->estacao.embarcando == embarcando,
               "houve reserva sem vaga disponivel");
        erro = pthread_cond_timedwait(&teste->progresso,
                                     &teste->estacao.trava, &prazo);
        exigir(erro == 0 || erro == ETIMEDOUT, "verificar bloqueio");
    } while (erro != ETIMEDOUT);
    exigir(teste->estacao.esperando == esperando &&
           teste->retornaram == retornaram &&
           teste->estacao.vagas == 0 &&
           teste->estacao.embarcando == embarcando,
           "houve reserva sem vaga disponivel");
    pthread_mutex_unlock(&teste->estacao.trava);
}

int main(void)
{
    /* Limita a duracao caso uma implementacao incorreta trave o teste. */
    alarm(10);
    struct teste teste = {0};
    pthread_t identificadores_passageiros[PASSAGEIROS];
    station_init(&teste.estacao);
    exigir(pthread_cond_init(&teste.progresso, NULL) == 0,
           "inicializar condicao do teste");

    for (int indice = 0; indice < PASSAGEIROS; indice++) {
        exigir(pthread_create(&identificadores_passageiros[indice], NULL, passageiro, &teste) == 0,
               "criar passageiro");
    }

    aguardar_estado(&teste, PASSAGEIROS, 0);
    puts("PASS: 6 passageiros aguardam sem vagas.");
    verificar_sem_vagas(&teste, PASSAGEIROS, 0, 0);
    puts("PASS: aviso sem vagas mantem os passageiros esperando.");

    /* Simula a oferta de dois assentos pelo carro. */
    pthread_mutex_lock(&teste.estacao.trava);
    teste.estacao.vagas = 2;
    pthread_cond_broadcast(&teste.estacao.carro_chegou);
    pthread_mutex_unlock(&teste.estacao.trava);

    aguardar_estado(&teste, 4, 2);
    verificar_sem_vagas(&teste, 4, 2, 2);
    puts("PASS: 2 vagas liberam exatamente 2 passageiros; 4 continuam esperando.");
    puts("PASS: 2 reservas concluidas antes de qualquer confirmacao de embarque.");

    /* Simula as confirmacoes anteriores e a oferta do proximo carro. */
    pthread_mutex_lock(&teste.estacao.trava);
    teste.estacao.embarcando = 0;
    teste.estacao.vagas = 4;
    pthread_cond_broadcast(&teste.estacao.carro_chegou);
    pthread_mutex_unlock(&teste.estacao.trava);

    aguardar_estado(&teste, 0, PASSAGEIROS);
    for (int indice = 0; indice < PASSAGEIROS; indice++) {
        exigir(pthread_join(identificadores_passageiros[indice], NULL) == 0, "finalizar passageiro");
    }
    exigir(teste.estacao.vagas == 0 && teste.estacao.embarcando == 4,
           "contadores finais incorretos");
    puts("PASS: as 4 vagas seguintes atendem todos os passageiros restantes.");

    pthread_cond_destroy(&teste.progresso);
    pthread_cond_destroy(&teste.estacao.carro_chegou);
    pthread_cond_destroy(&teste.estacao.embarque_concluido);
    pthread_mutex_destroy(&teste.estacao.trava);
    alarm(0);
    puts("Espera dos passageiros: PASS");
    return EXIT_SUCCESS;
}
