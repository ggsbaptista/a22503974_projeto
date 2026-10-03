#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>

#include "scheduler.h"
#include "msg.h"
#include "queue.h"

static volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
    printf("\n[Signal] Caught signal %d — stopping scheduler...\n", sig);
    keep_running = 0;
}

/*
 * Processa o argumento --sched e permite selecionar:
 * FIFO, SJF, RR ou MLFQ.
 */
int parse_args(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sched") == 0) {
            if (i + 1 < argc) {
                if (set_sched_algo(argv[++i]) < 0) {
                    fprintf(stderr, "Error: invalid scheduler: %s (use FIFO, SJF, RR or MLFQ)\n", argv[i]);
                    return -1;
                }
            } else {
                fprintf(stderr, "Error: --sched requires an algorithm name\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--sched FIFO|SJF|RR|MLFQ]\n", argv[0]);
            return 1;
        } else {
            fprintf(stderr, "Unknown option: %s\nTry --help\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int res = parse_args(argc, argv);
    if (res > 0) return EXIT_SUCCESS;
    if (res < 0) return EXIT_FAILURE;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("OSSIM scheduler configured with algorithm %s\n", get_sched_algo_str());

    /*
     * Filas utilizadas pelo simulador base:
     *
     * COMMAND:
     * processos à espera de enviar uma nova instrução.
     *
     * READY:
     * processos prontos para CPU nos algoritmos FIFO, SJF e RR.
     *
     * BLOCKED:
     * processos que estão a simular I/O.
     */
    queue_t command_queue = {0};
    queue_t ready_queue   = {0};
    queue_t blocked_queue = {0};

    /*
     * O MLFQ utiliza três READY queues separadas:
     *
     * Q0 = prioridade alta
     * Q1 = prioridade média
     * Q2 = prioridade baixa
     */
    queue_t mlfq_high = {0};
    queue_t mlfq_mid  = {0};
    queue_t mlfq_low  = {0};

    /*
     * Esta variável define para onde é enviado um novo pedido RUN.
     *
     * Em MLFQ, novos RUN entram em Q0.
     * Nos restantes algoritmos entram na READY queue normal.
     */
    queue_t *incoming_ready;

    if (strcmp(get_sched_algo_str(), "MLFQ") == 0)
        incoming_ready = &mlfq_high;
    else
        incoming_ready = &ready_queue;

    int server_fd = setup_server_socket(SOCKET_PATH);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to set up server socket\n");
        return EXIT_FAILURE;
    }
    printf("Scheduler server listening on %s...\n", SOCKET_PATH);
    reset_time();

    /*
     * O simulador possui um único CPU.
     * NULL significa que o CPU está atualmente livre.
     */
    pcb_t *cpu_task = NULL;

    while (keep_running) {
        uint32_t now = now_ms();

        /*
         * 1. Processar novos pedidos enviados pelas aplicações.
         */
        check_new_commands(&command_queue, &blocked_queue, incoming_ready, server_fd, now);

        /*
         * 2. Atualizar processos que estão bloqueados em I/O.
         */
        check_blocked_queue(&blocked_queue, &command_queue, now);

        /*
         * 3. Executar uma decisão/passo do algoritmo de escalonamento atual.
         */
        scheduler(
            now,
            &ready_queue,
            &command_queue,
            &cpu_task,
            &mlfq_high,
            &mlfq_mid,
            &mlfq_low
        );

        /*
         * Cada iteração do ciclo representa aproximadamente um tick
         * de TICKS_MS milissegundos.
         */
        usleep(TICKS_MS * 1000);
    }

    printf("[Scheduler] Cleaning up and shutting down...\n");
    close(server_fd);
    unlink(SOCKET_PATH);
    printf("[Scheduler] Shutdown complete.\n");
    return 0;
}