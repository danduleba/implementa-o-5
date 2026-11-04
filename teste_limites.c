/* Testes adicionais de concorrencia. Nao substituem o teste do professor.
 * Uso: ./teste_limites [repeticoes_aleatorias] [limite_de_passageiros]
 * Cada cenario roda em um processo separado, com limite de tempo.
 */
#include "sync_car.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct cenario;

struct argumento_passageiro {
    struct cenario *cenario;
    uint32_t semente;
};

struct cenario {
    struct station estacao;
    /* Este mutex pertence ao observador do teste, nao a struct station. */
    pthread_mutex_t observacao;
    pthread_cond_t progresso;
    pthread_t *passageiros;
    struct argumento_passageiro *argumentos;
    pthread_t carro;
    int limite;
    int criados;
    int reservados;
    int liberados;
    int confirmacoes_iniciadas;
    int concluidos;
    int capacidade;
    int meta;
    int carro_retornou;
    uint32_t semente;
};

static int repeticoes = 50;
static int limite_passageiros = 512;

static void falhar(const char *formato, ...)
{
    va_list argumentos;
    va_start(argumentos, formato);
    fputs("    FALHA: ", stderr);
    vfprintf(stderr, formato, argumentos);
    fputc('\n', stderr);
    va_end(argumentos);
    exit(EXIT_FAILURE);
}

static void conferir_pthread(int erro, const char *operacao)
{
    if (erro == EAGAIN || erro == ENOMEM) {
        fprintf(stderr, "    INCONCLUSIVO: recursos insuficientes para %s: %s\n",
                operacao, strerror(erro));
        exit(2);
    }
    if (erro != 0)
        falhar("%s: %s", operacao, strerror(erro));
}

static uint32_t sortear(uint32_t *semente)
{
    *semente ^= *semente << 13;
    *semente ^= *semente >> 17;
    *semente ^= *semente << 5;
    return *semente;
}

static void pausar_us(long microssegundos)
{
    struct timespec pausa = {
        microssegundos / 1000000L, (microssegundos % 1000000L) * 1000L
    };
    while (nanosleep(&pausa, &pausa) != 0) {
        if (errno != EINTR)
            falhar("nanosleep: %s", strerror(errno));
    }
}

static struct timespec prazo_ms(long milissegundos)
{
    struct timespec prazo;
    if (clock_gettime(CLOCK_REALTIME, &prazo) != 0)
        falhar("nao foi possivel consultar o relogio");
    prazo.tv_nsec += milissegundos * 1000000L;
    prazo.tv_sec += prazo.tv_nsec / 1000000000L;
    prazo.tv_nsec %= 1000000000L;
    return prazo;
}

static void iniciar_cenario(struct cenario *cenario, int limite, uint32_t semente)
{
    memset(cenario, 0, sizeof(*cenario));
    cenario->limite = limite;
    cenario->semente = semente | 1u;
    cenario->passageiros = calloc((size_t)limite, sizeof(*cenario->passageiros));
    cenario->argumentos = calloc((size_t)limite, sizeof(*cenario->argumentos));
    if (!cenario->passageiros || !cenario->argumentos) {
        fputs("    INCONCLUSIVO: memoria insuficiente para o teste\n", stderr);
        exit(2);
    }
    station_init(&cenario->estacao);
    conferir_pthread(pthread_mutex_init(&cenario->observacao, NULL),
                     "inicializar observador");
    conferir_pthread(pthread_cond_init(&cenario->progresso, NULL),
                     "inicializar notificacoes do teste");
}

static void *executar_passageiro(void *argumento)
{
    struct argumento_passageiro *dados = argumento;
    struct cenario *cenario = dados->cenario;
    uint32_t semente = dados->semente;
    station_wait_for_car(&cenario->estacao);

    pthread_mutex_lock(&cenario->observacao);
    int numero_reserva = ++cenario->reservados;
    if (numero_reserva > cenario->meta)
        falhar("reserva %d excedeu o limite autorizado de %d",
               numero_reserva, cenario->meta);
    pthread_cond_broadcast(&cenario->progresso);
    /* O controlador consegue atrasar inclusive a ultima confirmacao. */
    while (numero_reserva > cenario->liberados)
        pthread_cond_wait(&cenario->progresso, &cenario->observacao);
    cenario->confirmacoes_iniciadas++;
    pthread_mutex_unlock(&cenario->observacao);

    pausar_us((long)(sortear(&semente) % 1500u));
    station_on_board(&cenario->estacao);

    pthread_mutex_lock(&cenario->observacao);
    cenario->concluidos++;
    pthread_cond_broadcast(&cenario->progresso);
    pthread_mutex_unlock(&cenario->observacao);
    return NULL;
}

static void criar_passageiros(struct cenario *cenario, int quantidade)
{
    if (cenario->criados + quantidade > cenario->limite)
        falhar("limite interno do teste excedido");
    pthread_attr_t atributos;
    conferir_pthread(pthread_attr_init(&atributos), "inicializar atributos");
    /* Evita reservar varios gigabytes de pilhas na carga de centenas de threads. */
    conferir_pthread(pthread_attr_setstacksize(&atributos, 256u * 1024u),
                     "configurar pilha dos passageiros");
    for (int indice = 0; indice < quantidade; indice++) {
        int posicao = cenario->criados++;
        cenario->argumentos[posicao].cenario = cenario;
        cenario->argumentos[posicao].semente = sortear(&cenario->semente) | 1u;
        conferir_pthread(pthread_create(&cenario->passageiros[posicao], &atributos,
                                       executar_passageiro,
                                       &cenario->argumentos[posicao]),
                         "criar passageiro");
    }
    conferir_pthread(pthread_attr_destroy(&atributos), "liberar atributos");
}

static void aguardar_fila(struct cenario *cenario, int quantidade)
{
    /* Consulta protegida, com pausas; nao ha busy-wait. Garante que todos
       chegaram antes de oferecer vagas, evitando uma corrida no proprio teste. */
    for (int tentativa = 0; tentativa < 10000; tentativa++) {
        pthread_mutex_lock(&cenario->estacao.trava);
        int esperando = cenario->estacao.esperando;
        pthread_mutex_unlock(&cenario->estacao.trava);
        if (esperando == quantidade)
            return;
        pausar_us(1000);
    }
    falhar("fila nao atingiu %d passageiros dentro do prazo", quantidade);
}

static void aguardar_progresso(struct cenario *cenario, int reservas,
                              int conclusoes, int aguardar_carro)
{
    struct timespec prazo = prazo_ms(10000);
    pthread_mutex_lock(&cenario->observacao);
    while (cenario->reservados < reservas || cenario->concluidos < conclusoes ||
           (aguardar_carro && !cenario->carro_retornou)) {
        int erro = pthread_cond_timedwait(&cenario->progresso,
                                         &cenario->observacao, &prazo);
        if (erro == ETIMEDOUT)
            falhar("sem progresso: reservas=%d/%d, confirmacoes=%d/%d, carro=%d",
                   cenario->reservados, reservas, cenario->concluidos,
                   conclusoes, cenario->carro_retornou);
        conferir_pthread(erro, "aguardar progresso");
    }
    pthread_mutex_unlock(&cenario->observacao);
}

static void *executar_carro(void *argumento)
{
    struct cenario *cenario = argumento;
    station_load_car(&cenario->estacao, cenario->capacidade);
    pthread_mutex_lock(&cenario->observacao);
    if (cenario->reservados != cenario->meta)
        falhar("carro saiu com %d reservas acumuladas; esperado %d",
               cenario->reservados, cenario->meta);
    if (cenario->confirmacoes_iniciadas < cenario->meta)
        falhar("carro saiu antes de todos iniciarem a confirmacao do embarque");
    /* Concluidos e atualizado depois de station_on_board retornar. Nao se
       exige essa atualizacao antes da partida: isso criaria um falso erro. */
    cenario->carro_retornou = 1;
    pthread_cond_broadcast(&cenario->progresso);
    pthread_mutex_unlock(&cenario->observacao);
    return NULL;
}

static void iniciar_carro(struct cenario *cenario, int capacidade, int esperados)
{
    pthread_mutex_lock(&cenario->observacao);
    cenario->capacidade = capacidade;
    cenario->meta = cenario->reservados + esperados;
    cenario->carro_retornou = 0;
    pthread_mutex_unlock(&cenario->observacao);
    conferir_pthread(pthread_create(&cenario->carro, NULL, executar_carro, cenario),
                     "criar carro");
}

static void liberar_confirmacoes(struct cenario *cenario, int quantidade)
{
    pthread_mutex_lock(&cenario->observacao);
    cenario->liberados = quantidade;
    pthread_cond_broadcast(&cenario->progresso);
    pthread_mutex_unlock(&cenario->observacao);
}

static void verificar_carro_aguardando(struct cenario *cenario)
{
    struct timespec prazo = prazo_ms(60);
    pthread_mutex_lock(&cenario->observacao);
    for (;;) {
        if (cenario->carro_retornou)
            falhar("carro partiu com a ultima confirmacao ainda bloqueada");
        int erro = pthread_cond_timedwait(&cenario->progresso,
                                         &cenario->observacao, &prazo);
        if (erro == ETIMEDOUT) {
            if (cenario->carro_retornou)
                falhar("carro partiu antes da ultima confirmacao");
            break;
        }
        conferir_pthread(erro, "verificar ultima confirmacao");
    }
    pthread_mutex_unlock(&cenario->observacao);
}

static void encerrar_carro(struct cenario *cenario, int esperados, int atrasar_ultimo)
{
    int meta = cenario->meta;
    /* Todos devem conseguir reservar antes da primeira confirmacao. */
    aguardar_progresso(cenario, meta, meta - esperados, 0);
    if (atrasar_ultimo && esperados > 0) {
        liberar_confirmacoes(cenario, meta - 1);
        aguardar_progresso(cenario, meta, meta - 1, 0);
        pthread_mutex_lock(&cenario->estacao.trava);
        pthread_cond_broadcast(&cenario->estacao.embarque_concluido);
        pthread_mutex_unlock(&cenario->estacao.trava);
        verificar_carro_aguardando(cenario);
    }
    liberar_confirmacoes(cenario, meta);
    aguardar_progresso(cenario, meta, meta, 1);
    conferir_pthread(pthread_join(cenario->carro, NULL), "finalizar carro");
    pthread_mutex_lock(&cenario->estacao.trava);
    if (cenario->estacao.vagas != 0 || cenario->estacao.embarcando != 0 ||
        cenario->estacao.esperando < 0)
        falhar("contadores invalidos depois da partida do carro");
    pthread_mutex_unlock(&cenario->estacao.trava);
}

static void oferecer_carro(struct cenario *cenario, int capacidade,
                          int esperados, int atrasar_ultimo)
{
    iniciar_carro(cenario, capacidade, esperados);
    encerrar_carro(cenario, esperados, atrasar_ultimo);
}

static void finalizar_cenario(struct cenario *cenario)
{
    for (int indice = 0; indice < cenario->criados; indice++)
        conferir_pthread(pthread_join(cenario->passageiros[indice], NULL),
                         "finalizar passageiro");
    if (cenario->concluidos != cenario->criados)
        falhar("nem todos os passageiros foram atendidos");
    pthread_cond_destroy(&cenario->progresso);
    pthread_mutex_destroy(&cenario->observacao);
    pthread_cond_destroy(&cenario->estacao.carro_chegou);
    pthread_cond_destroy(&cenario->estacao.embarque_concluido);
    pthread_mutex_destroy(&cenario->estacao.trava);
    free(cenario->argumentos);
    free(cenario->passageiros);
}

static void estacao_vazia(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 1, 11);
    const int capacidades[] = {0, 1, 50, INT_MAX};
    for (size_t indice = 0; indice < sizeof capacidades / sizeof capacidades[0]; indice++)
        oferecer_carro(&cenario, capacidades[indice], 0, 0);
    finalizar_cenario(&cenario);
}

static void carro_sem_vagas(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 32, 13);
    criar_passageiros(&cenario, 32);
    aguardar_fila(&cenario, 32);
    oferecer_carro(&cenario, 0, 0, 0);
    aguardar_fila(&cenario, 32);
    oferecer_carro(&cenario, 32, 32, 0);
    finalizar_cenario(&cenario);
}

static void varios_carros(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 64, 17);
    criar_passageiros(&cenario, 64);
    aguardar_fila(&cenario, 64);
    const int capacidades[] = {3, 1, 7, 0, 13, 40};
    int restantes = 64;
    for (size_t indice = 0; indice < sizeof capacidades / sizeof capacidades[0]; indice++) {
        int quantidade = capacidades[indice];
        oferecer_carro(&cenario, quantidade, quantidade, 0);
        restantes -= quantidade;
        aguardar_fila(&cenario, restantes);
    }
    finalizar_cenario(&cenario);
}

static void vagas_excedentes(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 17, 19);
    criar_passageiros(&cenario, 17);
    aguardar_fila(&cenario, 17);
    oferecer_carro(&cenario, 1000, 17, 1);
    finalizar_cenario(&cenario);
}

static void capacidade_maxima(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 9, 23);
    criar_passageiros(&cenario, 9);
    aguardar_fila(&cenario, 9);
    oferecer_carro(&cenario, INT_MAX, 9, 1);
    finalizar_cenario(&cenario);
}

static void ultima_confirmacao(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 16, 29);
    criar_passageiros(&cenario, 16);
    aguardar_fila(&cenario, 16);
    oferecer_carro(&cenario, 16, 16, 1);
    finalizar_cenario(&cenario);
}

static void avisos_extras(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 16, 31);
    criar_passageiros(&cenario, 16);
    aguardar_fila(&cenario, 16);
    for (int indice = 0; indice < 20; indice++) {
        pthread_mutex_lock(&cenario.estacao.trava);
        pthread_cond_broadcast(&cenario.estacao.carro_chegou);
        pthread_mutex_unlock(&cenario.estacao.trava);
        pausar_us(1000);
    }
    pthread_mutex_lock(&cenario.observacao);
    if (cenario.reservados != 0)
        falhar("passageiros reservaram sem carro disponivel");
    pthread_mutex_unlock(&cenario.observacao);
    oferecer_carro(&cenario, 8, 8, 1);
    oferecer_carro(&cenario, 8, 8, 1);
    finalizar_cenario(&cenario);
}

static void chegadas_durante_embarque(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 7, 37);
    criar_passageiros(&cenario, 4);
    aguardar_fila(&cenario, 4);
    iniciar_carro(&cenario, 8, 7);
    aguardar_progresso(&cenario, 4, 0, 0);
    criar_passageiros(&cenario, 3);
    aguardar_progresso(&cenario, 7, 0, 0);
    encerrar_carro(&cenario, 7, 1);
    finalizar_cenario(&cenario);
}

static void passageiros_apos_partida(void)
{
    struct cenario cenario;
    iniciar_cenario(&cenario, 2, 41);
    criar_passageiros(&cenario, 1);
    aguardar_fila(&cenario, 1);
    oferecer_carro(&cenario, 10, 1, 0);
    criar_passageiros(&cenario, 1);
    aguardar_fila(&cenario, 1);
    oferecer_carro(&cenario, 1, 1, 0);
    finalizar_cenario(&cenario);
}

static void *controlar_estacao(void *argumento)
{
    unsigned int identificador = *(unsigned int *)argumento;
    struct cenario cenario;
    iniciar_cenario(&cenario, 48, 101u + identificador * 2u);
    criar_passageiros(&cenario, 48);
    aguardar_fila(&cenario, 48);
    oferecer_carro(&cenario, 0, 0, 0);
    oferecer_carro(&cenario, 5, 5, 0);
    oferecer_carro(&cenario, 11, 11, 0);
    oferecer_carro(&cenario, 100, 32, 1);
    finalizar_cenario(&cenario);
    return NULL;
}

static void estacoes_independentes(void)
{
    pthread_t controladores[4];
    unsigned int identificadores[4];
    for (unsigned int indice = 0; indice < 4; indice++) {
        identificadores[indice] = indice;
        conferir_pthread(pthread_create(&controladores[indice], NULL,
                                       controlar_estacao, &identificadores[indice]),
                         "criar controlador de estacao");
    }
    for (int indice = 0; indice < 4; indice++)
        conferir_pthread(pthread_join(controladores[indice], NULL),
                         "finalizar controlador de estacao");
}

static void carga_crescente(void)
{
    int quantidade = 1;
    for (;;) {
        struct cenario cenario;
        iniciar_cenario(&cenario, quantidade, (uint32_t)quantidade | 1u);
        criar_passageiros(&cenario, quantidade);
        aguardar_fila(&cenario, quantidade);
        oferecer_carro(&cenario, quantidade, quantidade, 0);
        finalizar_cenario(&cenario);
        printf("    Carga de %d passageiros: PASS\n", quantidade);
        if (quantidade == limite_passageiros)
            break;
        quantidade = quantidade > limite_passageiros / 2
                   ? limite_passageiros : quantidade * 2;
    }
}

static void rodadas_aleatorias(void)
{
    uint32_t semente = 20261104u;
    long total_passageiros = 0;
    long total_carros = 0;
    for (int rodada = 0; rodada < repeticoes; rodada++) {
        int quantidade = 32 + (int)(sortear(&semente) % 97u);
        struct cenario cenario;
        iniciar_cenario(&cenario, quantidade, sortear(&semente));
        criar_passageiros(&cenario, quantidade);
        aguardar_fila(&cenario, quantidade);
        int restantes = quantidade;
        int carros = 0;
        while (restantes > 0) {
            int capacidade = (carros % 8 == 7)
                           ? restantes : (int)(sortear(&semente) % 81u);
            int esperados = capacidade < restantes ? capacidade : restantes;
            oferecer_carro(&cenario, capacidade, esperados, 0);
            restantes -= esperados;
            aguardar_fila(&cenario, restantes);
            carros++;
        }
        total_carros += carros;
        total_passageiros += quantidade;
        finalizar_cenario(&cenario);
    }
    printf("    %d rodadas; %ld passageiros; %ld carros; semente inicial 20261104.\n",
           repeticoes, total_passageiros, total_carros);
}

static int ler_numero(const char *texto, int minimo, int maximo)
{
    char *fim;
    errno = 0;
    long valor = strtol(texto, &fim, 10);
    if (errno || *texto == '\0' || *fim != '\0' || valor < minimo || valor > maximo)
        falhar("argumento invalido: %s (esperado entre %d e %d)", texto, minimo, maximo);
    return (int)valor;
}

int main(int quantidade_argumentos, char **argumentos)
{
    if (quantidade_argumentos > 3) {
        fprintf(stderr, "Uso: %s [repeticoes:1..10000] [carga:1..4096]\n", argumentos[0]);
        return EXIT_FAILURE;
    }
    if (quantidade_argumentos > 1)
        repeticoes = ler_numero(argumentos[1], 1, 10000);
    if (quantidade_argumentos > 2)
        limite_passageiros = ler_numero(argumentos[2], 1, 4096);
    setvbuf(stdout, NULL, _IOLBF, 0);
    const struct {
        const char *nome;
        void (*executar)(void);
    } testes[] = {
        {"Estacao vazia: capacidades 0, 1, 50 e INT_MAX", estacao_vazia},
        {"Carro sem vagas com 32 passageiros esperando", carro_sem_vagas},
        {"Varios carros, vagas limitadas e reaproveitamento da estacao", varios_carros},
        {"Mais vagas do que passageiros", vagas_excedentes},
        {"Capacidade INT_MAX com passageiros esperando", capacidade_maxima},
        {"Ultima confirmacao atrasada impede a partida", ultima_confirmacao},
        {"Avisos extras nao autorizam reservas nem partida", avisos_extras},
        {"Novos passageiros chegam durante o embarque", chegadas_durante_embarque},
        {"Passageiro apos a partida precisa aguardar outro carro", passageiros_apos_partida},
        {"Quatro estacoes funcionando simultaneamente", estacoes_independentes},
        {"Carga crescente de passageiros simultaneos", carga_crescente},
        {"Rodadas aleatorias com confirmacoes concorrentes", rodadas_aleatorias}
    };
    int falhas = 0;
    int inconclusivos = 0;
    int total = (int)(sizeof testes / sizeof testes[0]);
    printf("TESTES DE LIMITES: %d rodadas aleatorias; carga maxima %d.\n",
           repeticoes, limite_passageiros);
    for (int indice = 0; indice < total; indice++) {
        printf("[%02d/%02d] %s\n", indice + 1, total, testes[indice].nome);
        pid_t processo = fork();
        if (processo < 0)
            falhar("fork: %s", strerror(errno));
        if (processo == 0) {
            alarm(testes[indice].executar == rodadas_aleatorias ? 300 : 40);
            testes[indice].executar();
            exit(EXIT_SUCCESS);
        }
        int estado;
        while (waitpid(processo, &estado, 0) < 0) {
            if (errno != EINTR)
                falhar("waitpid: %s", strerror(errno));
        }
        if (WIFEXITED(estado) && WEXITSTATUS(estado) == 0) {
            puts("    PASS");
        } else if (WIFEXITED(estado) && WEXITSTATUS(estado) == 2) {
            puts("    INCONCLUSIVO: limite de recursos do ambiente.");
            inconclusivos++;
        } else {
            if (WIFSIGNALED(estado))
                printf("    FALHA: processo encerrado pelo sinal %d (SIGALRM indica prazo excedido).\n",
                       WTERMSIG(estado));
            falhas++;
        }
    }
    printf("RESULTADO: %d/%d cenarios aprovados; %d falhas; %d inconclusivos.\n",
           total - falhas - inconclusivos, total, falhas, inconclusivos);
    if (falhas)
        return EXIT_FAILURE;
    return inconclusivos ? 2 : EXIT_SUCCESS;
}
