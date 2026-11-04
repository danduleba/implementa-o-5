# Sincronização do Metrô

Implementação em C com Pthreads para coordenar a reserva de assentos e a confirmação do embarque de passageiros. O carro aguarda os embarques antes de partir, respeitando as vagas disponíveis.

Desenvolvido e testado no Ubuntu pelo WSL, no Windows. Para compilar, é necessário ter GCC e Make instalados.

## Arquivos

- `sync_car.c`: implementação das quatro funções de sincronização.
- `sync_car.h`: estrutura da estação e declaração das funções.
- `teste_fornecido.c`: teste completo fornecido pelo professor, com 100 passageiros.
- `teste_init.c`: teste da inicialização da estação.
- `teste_espera.c`: testes parciais da espera e reserva de vagas.
- `teste_limites.c`: testes de concorrência, casos limite e cargas maiores, gerados com auxílio de IA.
- `Makefile`: comandos de compilação, testes e limpeza.
- `evidencias.log`: registro dos comandos e resultados obtidos.
- `iniciar_evidencias.ps1`: script opcional para iniciar a gravação no Ubuntu a partir do PowerShell.
- [dad.pdf](dad.pdf): relatório da implementação e dos testes realizados.

## Compilar e executar

No terminal Ubuntu, dentro da pasta do projeto:

```bash
make clean
make
./sync_car
```

O executável `sync_car` executa o teste do professor. Se todas as verificações passarem, a última mensagem será `Looks good!`.

## Testes adicionais

```bash
make -B testar-init testar-espera
make -B testar-limites
```

Para repetir a execução de estresse registrada no relatório:

```bash
make -B estressar REPETICOES=300 LIMITE_PASSAGEIROS=1024
```

O registro existente mostra 12/12 cenários aprovados, com 300 rodadas aleatórias e carga de até 1.024 passageiros simultâneos. Esses resultados descrevem as execuções realizadas, sem garantir todos os escalonamentos possíveis.

Para remover os executáveis gerados:

```bash
make clean
```
