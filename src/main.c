#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_CAPACITY 256
#define DEFAULT_TS_MS 50
#define DEFAULT_CS_MS 10
#define JOB_EXECUTION_MS 2
#define DEFAULT_RUNTIME_MS 10000
#define DEFAULT_ARRIVAL_MIN_MS 30
#define DEFAULT_ARRIVAL_MAX_MS 120

typedef struct {
	unsigned id;
	struct timespec arrival;
} request_t;

typedef struct {
	request_t items[QUEUE_CAPACITY];
	size_t head;
	size_t tail;
	size_t count;
	pthread_mutex_t mutex;
} request_queue_t;

typedef struct {
	long ts_ns;
	long cs_ns;
} server_config_t;

static request_queue_t queue = {
    .head = 0, .tail = 0, .count = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

static atomic_bool stopping = false;
static atomic_uint generated = 0;
static atomic_uint executed = 0;
static atomic_uint dropped = 0;
static atomic_uint max_queue_seen = 0;

static long timespec_diff_ns(struct timespec a, struct timespec b)
{
	return (a.tv_sec - b.tv_sec) * 1000000000L + (a.tv_nsec - b.tv_nsec);
}

static void timespec_add_ns(struct timespec *t, long ns)
{
	t->tv_sec += ns / 1000000000L;
	t->tv_nsec += ns % 1000000000L;
	if (t->tv_nsec >= 1000000000L) {
		t->tv_sec++;
		t->tv_nsec -= 1000000000L;
	}
}

static void sleep_ms(unsigned ms)
{
	struct timespec req = {
		.tv_sec = (time_t)(ms / 1000),
		.tv_nsec = (long)(ms % 1000) * 1000000L
	};

	while (nanosleep(&req, &req) == -1 && errno == EINTR) {
		continue;
	}
}

static bool enqueue_request(request_t request)
{
	bool inserted = false;

	pthread_mutex_lock(&queue.mutex);
	if (queue.count < QUEUE_CAPACITY) {
		queue.items[queue.tail] = request;
		queue.tail = (queue.tail + 1) % QUEUE_CAPACITY;
		queue.count++;
		inserted = true;

		unsigned current = (unsigned)queue.count;
		unsigned previous = atomic_load(&max_queue_seen);
		while (current > previous && !atomic_compare_exchange_weak(&max_queue_seen, &previous, current)) {
			continue;
		}
	}
	pthread_mutex_unlock(&queue.mutex);

	return inserted;
}

static bool dequeue_request(request_t *request)
{
	bool found = false;

	pthread_mutex_lock(&queue.mutex);
	if (queue.count > 0) {
		*request = queue.items[queue.head];
		queue.head = (queue.head + 1) % QUEUE_CAPACITY;
		queue.count--;
		found = true;
	}
	pthread_mutex_unlock(&queue.mutex);

	return found;
}

static size_t queue_size(void)
{
	pthread_mutex_lock(&queue.mutex);
	size_t count = queue.count;
	pthread_mutex_unlock(&queue.mutex);
	return count;
}

/* Simula a função aperiódica escolhida: SORT_ACT (desviar uma peça).
   O sleep representa o tempo de execução do acionamento simulado. */
static void execute_sort_act(const request_t *request)
{
	printf("[SORT_ACT] desviando peça %u\n", request->id);
	fflush(stdout);
	sleep_ms(JOB_EXECUTION_MS);
}

static void *request_generator(void *arg)
{
	const unsigned *range = arg;
	const unsigned min_ms = range[0];
	const unsigned max_ms = range[1];

	while (!atomic_load(&stopping)) {
		unsigned delay = min_ms;

		if (max_ms > min_ms) {
		    delay += (unsigned)(rand() % (int)(max_ms - min_ms + 1));
		}
		sleep_ms(delay);

		if (atomic_load(&stopping))
		    break;

		request_t request = {
		    .id = atomic_fetch_add(&generated, 1) + 1
		};

		clock_gettime(CLOCK_MONOTONIC, &request.arrival);

		if (enqueue_request(request)) {
		    printf("[GERADOR] requisição %u criada | fila=%zu\n", request.id, queue_size());
		}
		else {
		    atomic_fetch_add(&dropped, 1);
		    printf("[GERADOR] fila cheia: requisição %u descartada\n", request.id);
		}
	}

	return NULL;
}

static void *periodic_server(void *arg)
{
	const server_config_t *config = arg;
	struct timespec next_release;

	clock_gettime(CLOCK_MONOTONIC, &next_release);

	while (!atomic_load(&stopping)) {
		/* Uma ativação do servidor a cada Ts. */
		long consumed_ns = 0;
		unsigned jobs_this_period = 0;

		printf("\n[SERVIDOR] ativado | orçamento=%.2f ms | fila=%zu\n", config->cs_ns / 1000000.0, queue_size());

		while (!atomic_load(&stopping) && consumed_ns < config->cs_ns) {
			request_t request;
			if (!dequeue_request(&request))
				break;

			struct timespec before, after;
			clock_gettime(CLOCK_MONOTONIC, &before);
			execute_sort_act(&request);
			clock_gettime(CLOCK_MONOTONIC, &after);

			long elapsed = timespec_diff_ns(after, before);
			consumed_ns += elapsed;
			jobs_this_period++;
			atomic_fetch_add(&executed, 1);

			long response_ns = timespec_diff_ns(after, request.arrival);
			printf("[SERVIDOR] peça=%u | execução=%.2f ms | resposta=%.2f ms\n", request.id, elapsed / 1000000.0, response_ns / 1000000.0);
		}

		printf("[SERVIDOR] fim do atendimento | jobs=%u | orçamento usado=%.2f/%.2f ms | fila=%zu\n", jobs_this_period, consumed_ns / 1000000.0, config->cs_ns / 1000000.0, queue_size());

		/* Avança a liberação por tempo absoluto para evitar deriva. */
		timespec_add_ns(&next_release, config->ts_ns);
		if (!atomic_load(&stopping)) {
			int rc;
			do {
				rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_release, NULL);
			} while (rc == EINTR && !atomic_load(&stopping));
		}
	}

	return NULL;
}

static unsigned parse_unsigned(const char *text, const char *name)
{
	char *end = NULL;
	errno = 0;
	unsigned long value = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || value > 1000000UL) {
		fprintf(stderr, "Valor inválido para %s: %s\n", name, text);
		exit(EXIT_FAILURE);
	}
	return (unsigned)value;
}

int main(int argc, char **argv)
{
	unsigned min_arrival = DEFAULT_ARRIVAL_MIN_MS;
	unsigned max_arrival = DEFAULT_ARRIVAL_MAX_MS;
	unsigned runtime_ms = DEFAULT_RUNTIME_MS;

	if (argc > 1)
		min_arrival = parse_unsigned(argv[1], "intervalo mínimo");

	if (argc > 2)
		max_arrival = parse_unsigned(argv[2], "intervalo máximo");

	if (argc > 3)
		runtime_ms = parse_unsigned(argv[3], "duração");

	if (min_arrival == 0 || max_arrival < min_arrival || runtime_ms == 0) {
		fprintf(stderr, "Uso: %s [intervalo_min_ms intervalo_max_ms duracao_ms]\n" "Exemplo: %s 10 30 10000\n", argv[0], argv[0]);
		return EXIT_FAILURE;
	}

	srand((unsigned)time(NULL));

	const server_config_t config = {
		.ts_ns = DEFAULT_TS_MS * 1000000L,
		.cs_ns = DEFAULT_CS_MS * 1000000L
	};
	const unsigned arrival_range[2] = { min_arrival, max_arrival };

	printf("=== M3: SERVIDOR PERIODICO PARA REQUISICOES APERIODICAS ===\n");
	printf("Função simulada: SORT_ACT (desvio de peça)\n");
	printf("Ts = %d ms | Cs = %d ms | utilização reservada Cs/Ts = %.1f%%\n", DEFAULT_TS_MS, DEFAULT_CS_MS, 100.0 * DEFAULT_CS_MS / DEFAULT_TS_MS);
	printf("Chegada das requisições: intervalo aleatório de %u a %u ms\n", min_arrival, max_arrival);
	printf("Duração do teste: %u ms\n", runtime_ms);
	printf("Tempo simulado de cada acionamento: %d ms\n\n", JOB_EXECUTION_MS);

	pthread_t generator_thread;
	pthread_t server_thread;

	if (pthread_create(&server_thread, NULL, periodic_server, (void *)&config) != 0) {
		perror("pthread_create servidor");
		return EXIT_FAILURE;
	}

	if (pthread_create(&generator_thread, NULL, request_generator, (void *)arrival_range) != 0) {
		perror("pthread_create gerador");
		atomic_store(&stopping, true);
		pthread_join(server_thread, NULL);
		return EXIT_FAILURE;
	}

	sleep_ms(runtime_ms);
	atomic_store(&stopping, true);

	pthread_join(generator_thread, NULL);
	pthread_join(server_thread, NULL);

	printf("\n================ RESUMO DO TESTE ================\n");
	printf("Requisições geradas:       %u\n", atomic_load(&generated));
	printf("SORT_ACT executadas:       %u\n", atomic_load(&executed));
	printf("Requisições descartadas:   %u\n", atomic_load(&dropped));
	printf("Requisições ainda na fila: %zu\n", queue_size());
	printf("Maior tamanho da fila:     %u\n", atomic_load(&max_queue_seen));
	printf("Utilização reservada:      %.1f%% (Cs/Ts)\n", 100.0 * DEFAULT_CS_MS / DEFAULT_TS_MS);
	printf("==================================================\n");

	return EXIT_SUCCESS;
}
