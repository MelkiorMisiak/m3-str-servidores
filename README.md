# Projeto M3 — Servidor periódico para tarefas aperiódicas

## Objetivo

Este projeto demonstra, em C com `pthread`, o atendimento de requisições
aperiódicas por um servidor periódico. A função escolhida para representar
uma função do M2 é `SORT_ACT`: o acionamento simulado de um desviador de peças.

O programa é independente do ESP32. O gerador de requisições simula a chegada
de peças; o servidor periódico retira as solicitações de uma fila e executa
a função `SORT_ACT` dentro do orçamento de cada período.

## Requisitos

- Linux
- GCC
- Make
- biblioteca pthread (normalmente já disponível com GCC/glibc)

## Compilar

Na pasta do projeto:

```bash
make
```

Ou manualmente:

```bash
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror src/main.c -o m3 -pthread
```

## Executar

Execução padrão (requisições chegam em intervalos aleatórios de 30 a 120 ms,
durante 10 segundos):

```bash
./m3
```

Também é possível alterar a carga:

```bash
./m3 INTERVALO_MIN_MS INTERVALO_MAX_MS DURACAO_MS
```

Exemplos:

```bash
./m3 100 200 10000   # carga baixa
./m3 30 120 10000    # carga moderada (padrão)
./m3 10 30 10000     # carga alta
./m3 5 15 10000      # carga muito alta
```

Quanto menor o intervalo entre requisições, maior a carga oferecida ao servidor.

## Parâmetros do servidor

No início de `src/main.c`:

```c
#define DEFAULT_TS_MS 50
#define DEFAULT_CS_MS 10
#define JOB_EXECUTION_MS 2
```

- `Ts = 50 ms`: o servidor é liberado a cada 50 ms.
- `Cs = 10 ms`: orçamento de atendimento por período.
- `JOB_EXECUTION_MS = 2 ms`: duração simulada de cada acionamento.

A utilização reservada pelo servidor é:

`U = Cs / Ts = 10 / 50 = 0,20 = 20%`

Isso é a fração de CPU reservada pelo orçamento, não uma medição direta da
utilização total do processador.

## O que observar

- `[GERADOR]`: criação de solicitações aperiódicas.
- `[SERVIDOR] ativado`: início de uma nova ativação periódica.
- `[SORT_ACT]`: execução simulada do desvio.
- `orçamento usado`: tempo medido pelo servidor durante aquela ativação.
- `fila`: quantidade de solicitações aguardando.
- resumo final: geradas, executadas, descartadas e maior tamanho da fila.

Faça capturas de tela de pelo menos três execuções: carga baixa, moderada e
alta. Compare os resultados no relatório.

## Observação importante sobre o orçamento

Os jobs são simulados como operações não preemptivas de aproximadamente 2 ms.
O servidor mede o tempo de cada job e não inicia outro quando o orçamento já
foi consumido. Como a execução de um job não é interrompida no meio, o tempo
medido pode ultrapassar ligeiramente `Cs` por causa do último job e da
sobrecarga do sistema operacional. Isso deve ser comentado na análise.

## Relação com o M2

No M2, `SORT_ACT` fazia parte do sistema da esteira. Neste M3, não copiamos o
firmware nem controlamos hardware: reproduzimos a lógica de solicitação e
atendimento do acionamento do desviador em uma simulação no computador,
agora limitada por um servidor periódico.

## Entrega acadêmica

Além do código, o enunciado pede relatório com identificação, enunciado,
imagens/tabelas de resultados, explicação dos resultados e descrição das
técnicas utilizadas. Também exige evidências dos testes, publicação no GitHub
e compilação durante a apresentação.
