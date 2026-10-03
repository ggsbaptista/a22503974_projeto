#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "queue.h"

/*
 * Quantum utilizado pelos algoritmos preemptivos RR e MLFQ.
 * FIFO e SJF não utilizam time slice.
 */
#define TIME_SLICE_MS 500

/*
 * Intervalo entre reforços periódicos de prioridade no MLFQ.
 * A cada 5 segundos, os processos de Q1 e Q2 regressam a Q0,
 * reduzindo o risco de starvation.
 */
#define MLFQ_BOOST_INTERVAL_MS 5000

/*
 * Algoritmos de escalonamento suportados pelo simulador.
 */
typedef enum {
    SCHED_FIFO = 0,
    SCHED_SJF,
    SCHED_RR,
    SCHED_MLFQ,
} sched_algo_en;

/*
 * Define o algoritmo escolhido através do argumento --sched.
 * Retorna -1 se o nome recebido não corresponder a um algoritmo válido.
 */
int  set_sched_algo(const char *name);

const char *get_sched_algo_str(void);

/**
 * @brief Executa um passo do escalonador.
 *
 * A função é chamada uma vez por tick do simulador.
 *
 * @param current_time_ms tempo atual da simulação
 * @param rq READY queue utilizada por FIFO, SJF e RR
 * @param cq COMMAND queue
 * @param cpu_task processo atualmente no CPU, ou NULL se estiver livre
 * @param mlfq_high fila Q0 do MLFQ, prioridade alta
 * @param mlfq_mid fila Q1 do MLFQ, prioridade média
 * @param mlfq_low fila Q2 do MLFQ, prioridade baixa
 *
 * @return 1 se um novo processo foi colocado no CPU, 0 caso contrário
 */
int scheduler(
    uint32_t current_time_ms,
    queue_t *rq,
    queue_t *cq,
    pcb_t **cpu_task,
    queue_t *mlfq_high,
    queue_t *mlfq_mid,
    queue_t *mlfq_low
);

#endif //SCHEDULER_H