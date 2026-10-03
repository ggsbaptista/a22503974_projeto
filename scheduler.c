#include "scheduler.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "msg.h"

/*
 * Algoritmos de escalonamento suportados pelo simulador.
 * O valor do enum sched_algo_en corresponde à posição neste vetor.
 */
static const char *SCHED_NAMES[] = { "FIFO", "SJF", "RR", "MLFQ", NULL };
static sched_algo_en sched_algo = SCHED_FIFO;

/*
 * Seleciona o algoritmo recebido através do parâmetro --sched.
 * A comparação não distingue maiúsculas de minúsculas.
 */
int set_sched_algo(const char *name) {
    for (int i = 0; SCHED_NAMES[i] != NULL; i++) {
        if (strcasecmp(SCHED_NAMES[i], name) == 0) {
            sched_algo = (sched_algo_en) i;
            return sched_algo;
        }
    }
    return -1;
}

const char *get_sched_algo_str(void) {
    return SCHED_NAMES[sched_algo];
}

/**
 * Send DONE to the application and move the finished burst to the command queue.
 *
 * Quando um burst termina, a aplicação recebe DONE e o PCB regressa à
 * COMMAND queue, onde ficará à espera do próximo pedido RUN ou BLOCK.
 */
static void finish_burst(uint32_t current_time_ms, queue_t *cq, pcb_t *task) {
    msg_t msg = {
        .pid = task->pid,
        .request = PROCESS_REQUEST_DONE,
        .time_ms = current_time_ms
    };
    if (write(task->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
        perror("write");
    }
    enqueue_pcb(cq, task);
}

/**
 * Função principal do escalonador.
 *
 * É chamada uma vez por tick do simulador e implementa os quatro algoritmos:
 *
 * FIFO:
 *   - não preemptivo;
 *   - seleciona o processo que está há mais tempo na READY queue.
 *
 * SJF:
 *   - não preemptivo;
 *   - seleciona o processo READY com o menor burst de CPU (time_ms).
 *
 * RR:
 *   - preemptivo;
 *   - utiliza um quantum TIME_SLICE_MS;
 *   - quando o quantum termina, o processo volta para o fim da READY queue.
 *
 * MLFQ:
 *   - utiliza três níveis de prioridade: Q0, Q1 e Q2;
 *   - Q0 tem prioridade superior a Q1 e Q1 superior a Q2;
 *   - um processo que esgota o quantum perde prioridade:
 *       Q0 -> Q1
 *       Q1 -> Q2
 *       Q2 -> Q2
 */
int scheduler(
    uint32_t current_time_ms,
    queue_t *rq,
    queue_t *cq,
    pcb_t **cpu_task,
    queue_t *mlfq_high,
    queue_t *mlfq_mid,
    queue_t *mlfq_low
) {

    /*
     * Se existe um processo a executar no CPU, contabilizamos mais um tick
     * no tempo de CPU já executado durante o burst atual.
     */
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        /*
         * O teste de conclusão é efetuado antes do teste do quantum.
         * Assim, se o burst terminar neste tick, termina normalmente em vez
         * de ser preemptado e reenviado para uma fila.
         */
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {

            printf("Time [ms]: %d\tPID: %d\tSTOP_RUNNING\n",
                   current_time_ms, (*cpu_task)->pid);

            finish_burst(current_time_ms, cq, *cpu_task);
            *cpu_task = NULL;
        }

        /*
         * ROUND ROBIN
         *
         * slice_start_ms guarda o instante em que começou o quantum atual.
         * Quando são atingidos TIME_SLICE_MS, o processo é preemptado e
         * colocado no fim da READY queue, garantindo rotação entre processos.
         */
        else if (sched_algo == SCHED_RR &&
                 current_time_ms - (*cpu_task)->slice_start_ms >= TIME_SLICE_MS) {

            printf("Time [ms]: %d\tPID: %d\tTIME_SLICE_EXPIRED\n",
                   current_time_ms, (*cpu_task)->pid);

            enqueue_pcb(rq, *cpu_task);
            *cpu_task = NULL;
                 }

        /*
         * MLFQ
         *
         * Quando um processo utiliza todo o quantum, o seu nível de prioridade
         * é reduzido. Processos que já estão em Q2 permanecem em Q2.
         *
         * Esta é a parte do código responsável pelo mecanismo de feedback:
         * processos CPU-bound tendem a descer de prioridade por utilizarem
         * repetidamente todo o quantum disponível.
         */
        else if (sched_algo == SCHED_MLFQ &&
                 current_time_ms - (*cpu_task)->slice_start_ms >= TIME_SLICE_MS) {

            if ((*cpu_task)->mlfq_level == 0) {
                (*cpu_task)->mlfq_level = 1;
                enqueue_pcb(mlfq_mid, *cpu_task);

                printf("PID %d desceu de Q0 para Q1\n",
                       (*cpu_task)->pid);
            }

            else if ((*cpu_task)->mlfq_level == 1) {
                (*cpu_task)->mlfq_level = 2;
                enqueue_pcb(mlfq_low, *cpu_task);

                printf("PID %d desceu de Q1 para Q2\n",
                       (*cpu_task)->pid);
            }

            else {
                enqueue_pcb(mlfq_low, *cpu_task);

                printf("PID %d manteve-se em Q2\n",
                       (*cpu_task)->pid);
            }

            *cpu_task = NULL;
                 }
    }

    /*
     * Se o CPU está livre, escolhemos o próximo processo de acordo
     * com o algoritmo de escalonamento selecionado.
     */
    if (*cpu_task == NULL) {

        /*
         * FIFO e RR retiram o primeiro elemento da READY queue.
         * A diferença é que FIFO nunca preempta o processo enquanto RR
         * pode reenviá-lo para o fim da fila quando termina o quantum.
         */
        if ((sched_algo == SCHED_FIFO) ||
     (sched_algo == SCHED_RR)) {

            *cpu_task = dequeue_pcb(rq);

     /*
      * SJF procura na READY queue o processo cujo burst atual possui
      * o menor time_ms.
      */
     } else if (sched_algo == SCHED_SJF) {

         *cpu_task = dequeue_pcb_sjf(rq);

     /*
      * MLFQ escolhe sempre a fila de maior prioridade que não esteja vazia:
      * primeiro Q0, depois Q1 e por último Q2.
      */
     } else if (sched_algo == SCHED_MLFQ) {

         if (mlfq_high->head != NULL) {
             *cpu_task = dequeue_pcb(mlfq_high);

             /*
              * Um processo retirado de Q0 fica identificado com nível 0.
              */
             (*cpu_task)->mlfq_level = 0;
         }
         else if (mlfq_mid->head != NULL) {
             *cpu_task = dequeue_pcb(mlfq_mid);
         }
         else if (mlfq_low->head != NULL) {
             *cpu_task = dequeue_pcb(mlfq_low);
         }

     } else {
         printf("Scheduling algorithm not implemented yet.\n");
     }

        /*
         * Quando um processo é colocado no CPU, guardamos o instante
         * inicial do seu novo quantum. Este valor é utilizado pelo RR
         * e pelo MLFQ para detetar o fim do time slice.
         */
        if (*cpu_task != NULL) {

            printf("R4 - Processo PID %d foi retirado da READY e vai executar no CPU\n",
                   (*cpu_task)->pid);

            (*cpu_task)->slice_start_ms = current_time_ms;

            printf("Time [ms]: %d\tPID: %d\tSTART_RUNNING\n",
                   current_time_ms, (*cpu_task)->pid);

            return 1;
        }
    }

    return 0;
}