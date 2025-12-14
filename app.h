#ifndef APP_H
#define APP_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <pthread.h>
#include <string.h>

// Σταθερές
#define KEY_IPC 1234
#define MAX_MESSAGES 100
#define MAX_PARTICIPANTS 10
#define PAYLOAD_SIZE 256
#define MAX_DIALOGUES 10


// Απαραίτητες δομές δεδομένων

// Δομη ενός μηνύματος
typedef struct {
    int dialogue_id;
    int message_id;
    pid_t sender_pid;
    int readers_count;
    char payload[PAYLOAD_SIZE];
} message_t;


// Δομή ενός διαλόγου
typedef struct {
    int dialogue_id;
    int num_participants;
    pid_t participant_pids[MAX_PARTICIPANTS];
    int is_active;
} dialogue_t;


// Δομή κοινών δεδομένων
typedef struct {
    int global_message_counter;
    int next_dialogue_id;
    int active_dialogue_count;
    dialogue_t dialogues[MAX_DIALOGUES];
    message_t message_buffer[MAX_MESSAGES];
} shared_data_t;


// Δεδομένα της κάθε διεργασίας
typedef struct {
    int shmid;
    int semid;
    shared_data_t *shm_ptr;
    int my_dialogue_id;
    int last_MID_read;
    int my_dialogue_index;
    int is_running;
    pthread_mutex_t local_mutex;
    pthread_t sender_tid;
    pthread_t receiver_tid;
    int needs_global_cleanup;
} process_data_t;

// Συναρτήσεις που καλούνται από τη main
void* sender_thread(void* arg);
void* receiver_thread(void* arg);
int init_ipc(process_data_t *ctx);
int join_dialogue(process_data_t *ctx);
void cleanup_ipc(process_data_t *ctx);

#endif
