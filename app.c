#include "app.h"

// Δομή union για τους σεμαφόρους
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

// Βοηθητική συνάρτηση sem_wait
// Μειώνει την τιμή του σεμαφόρου και μπλοκάρει αν είναι μηδενικός
// Χρησιμοποιείται για είσοδο σε κρίσιμη περιοχή
int sem_wait(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;

    if (semop(semid, &op, 1) == -1) {
        perror("sem_wait failed");
        return -1;
    }
    return 0;
}

// Βοηθητική συνάρτηση sem_signal
// Αυξάνει την τιμή του σεμαφόρου και ξυπνά μία διεργασία που περιμένει
int sem_signal(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;
    op.sem_flg = 0;

    if (semop(semid, &op, 1) == -1) {
        perror("sem_signal failed");
        return -1;
    }
    return 0;
}

// Συνάρτηση που δημιουργεί ή συνδέεται σε κοινή μνήμη και σεμαφόρους
// Η πρώτη διεργασία αρχικοποιεί τα δεδομένα της κοινής μνήμης

int init_ipc(process_data_t *ctx) {
    key_t key = KEY_IPC;
    int first_process = 0;

    // Δημιουργία ή σύνδεση σε κονόχρηστη μνήμη
    ctx->shmid = shmget(key, sizeof(shared_data_t), 0666);
    if (ctx->shmid == -1) {
        ctx->shmid = shmget(key, sizeof(shared_data_t), 0666 | IPC_CREAT | IPC_EXCL);
        if (ctx->shmid == -1) {
            perror("shmget failed");
            return -1;
        }
        first_process = 1;
    }

    // Προσάρτηση της κοινής μνήμης
    ctx->shm_ptr = (shared_data_t*) shmat(ctx->shmid, NULL, 0);
    if (ctx->shm_ptr == (shared_data_t*) -1) {
        perror("shmat failed");
        if (first_process) shmctl(ctx->shmid, IPC_RMID, NULL);
        return -1;
    }

    // Αρχικοποίηση κοινής μνήμης από τη πρώτη διεργασία
    if (first_process) {
        printf("Κοινή Μνήμη δημιουργήθηκε (ID: %d).\n", ctx->shmid);
        memset(ctx->shm_ptr, 0, sizeof(shared_data_t));
        ctx->shm_ptr->global_message_counter = 0;
        ctx->shm_ptr->next_dialogue_id = 1;
        ctx->shm_ptr->active_dialogue_count = 0;
    } else {
        printf("Σύνδεση με υπάρχουσα Κοινή Μνήμη (ID: %d).\n", ctx->shmid);
    }

    // Δημιουργία ή σύνδεση σε σεμαφόρους
    ctx->semid = semget(key, 3, 0666);
    if (ctx->semid == -1) {
        ctx->semid = semget(key, 3, 0666 | IPC_CREAT | IPC_EXCL);
        if (ctx->semid == -1) {
            perror("semget failed");
            shmdt(ctx->shm_ptr);
            return -1;
        }

        printf("Semaphore Set δημιουργήθηκε (ID: %d).\n", ctx->semid);

        // Αρχικές τιμές σεμαφόρων:
        // [0] mutex = 1
        // [1] notify = 0
        // [2] message counter = MAX_MESSAGES
        union semun arg;
        unsigned short values[3] = {1, 0, MAX_MESSAGES};
        arg.array = values;

        if (semctl(ctx->semid, 0, SETALL, arg) == -1) {
            perror("semctl SETALL failed");
            shmdt(ctx->shm_ptr);
            semctl(ctx->semid, 0, IPC_RMID);
            return -1;
        }
    } else {
        printf("Σύνδεση με υπάρχον Semaphore Set (ID: %d).\n", ctx->semid);
    }

    return 0;
}

// Συνάρτηση που εισάγει μια διεργασία σε διάλογο
// Αν dialogue_id = 0 δημιουργείται νέος διάλογος

int join_dialogue(process_data_t *ctx) {
    shared_data_t *shm = ctx->shm_ptr;
    pid_t my_pid = getpid();
    int target_index = -1;
    int result = 0;

    // Είσοδος σε κρίσιμη περιοχή
    if (sem_wait(ctx->semid, 0) == -1) {
        return -1;
    }

    // Δημιουργία νέου διαλόγου
    if (ctx->my_dialogue_id == 0) {
        if (shm->active_dialogue_count >= MAX_DIALOGUES) {
            fprintf(stderr, "Max dialogues reached.\n");
            result = -1;
            sem_signal(ctx->semid, 0);
            return result;
        }

        for (int i = 0; i < MAX_DIALOGUES; i++) {
            if (shm->dialogues[i].is_active == 0) {
                target_index = i;
                break;
            }
        }

        dialogue_t *dlg = &shm->dialogues[target_index];
        ctx->my_dialogue_id = shm->next_dialogue_id++;
        shm->active_dialogue_count++;

        dlg->dialogue_id = ctx->my_dialogue_id;
        dlg->num_participants = 1;
        dlg->participant_pids[0] = my_pid;
        dlg->is_active = 1;

        ctx->my_dialogue_index = target_index;
        ctx->last_MID_read = shm->global_message_counter;

    } 
    // Σύνδεση σε υπάρχον διάλογο
    else {
        for (int i = 0; i < MAX_DIALOGUES; i++) {
            dialogue_t *dlg = &shm->dialogues[i];
            if (dlg->is_active && dlg->dialogue_id == ctx->my_dialogue_id) {

                if (dlg->num_participants >= MAX_PARTICIPANTS) {
                    fprintf(stderr, "Dialogue full.\n");
                    result = -1;
                    sem_signal(ctx->semid, 0);
                    return result;
                }

                dlg->participant_pids[dlg->num_participants++] = my_pid;
                ctx->my_dialogue_index = i;
                ctx->last_MID_read = shm->global_message_counter;
                break;
            }
        }
    }

    sem_signal(ctx->semid, 0);
    return result;
}

// Συνάρτηση που διαβάζει από την είσοδο χρήστη και γράφει μήνυμα στην κοινή μνήμη
// Μετά την αποστολή ειδοποιεί όλους τους συμμετέχοντες (broadcast)

void* sender_thread(void* arg) {
    process_data_t *ctx = (process_data_t*) arg;
    shared_data_t *shm = ctx->shm_ptr;
    char input_buffer[PAYLOAD_SIZE];

    while (ctx->is_running) {
        printf("\n[%d - Διάλογος %d] Πληκτρολογήστε μήνυμα:\n> ",
               getpid(), ctx->my_dialogue_id);
        fflush(stdout);

        if (fgets(input_buffer, PAYLOAD_SIZE, stdin) == NULL) break;
        input_buffer[strcspn(input_buffer, "\n")] = '\0';

        int is_terminate = strcmp(input_buffer, "TERMINATE") == 0;

        sem_wait(ctx->semid, 0);

        int mid = ++shm->global_message_counter;
        int index = (mid - 1) % MAX_MESSAGES;
        int participants = shm->dialogues[ctx->my_dialogue_index].num_participants;

        message_t *msg = &shm->message_buffer[index];
        msg->message_id = mid;
        msg->dialogue_id = ctx->my_dialogue_id;
        msg->sender_pid = getpid();
        msg->readers_count = participants;
        strncpy(msg->payload, input_buffer, PAYLOAD_SIZE);

        if (is_terminate)
            shm->dialogues[ctx->my_dialogue_index].is_active = 0;

        sem_signal(ctx->semid, 0);

        // Broadcast: ειδοποίηση όλων των διεργασιών που περιμένουν
        for (int i = 0; i < participants; i++)
            sem_signal(ctx->semid, 1);

        if (is_terminate) {
            ctx->is_running = 0;
            break;
        }
    }
    return NULL;
}

// Συνάρτηση για ανάγνωση μηνύματος απο την κοινή μνήμη έπειτα απο ειδοποίηση
void* receiver_thread(void* arg) {
    process_data_t *ctx = (process_data_t*) arg;
    shared_data_t *shm = ctx->shm_ptr;

    while (ctx->is_running) {
        sem_wait(ctx->semid, 1);
        sem_wait(ctx->semid, 0);

        int target_mid = ctx->last_MID_read + 1;

        if (target_mid <= shm->global_message_counter) {
            int index = (target_mid - 1) % MAX_MESSAGES;
            message_t *msg = &shm->message_buffer[index];

            if (msg->dialogue_id == ctx->my_dialogue_id &&
                msg->sender_pid != getpid()) {

                printf("\n[%d] %s\n", msg->sender_pid, msg->payload);

                if (strcmp(msg->payload, "TERMINATE") == 0)
                    ctx->is_running = 0;
            }

            ctx->last_MID_read++;
            msg->readers_count--;

            if (msg->readers_count == 0 && !shm->dialogues[ctx->my_dialogue_index].is_active) {
                shm->active_dialogue_count--;
                if (shm->active_dialogue_count == 0)
                    ctx->needs_global_cleanup = 1;
            }
        }

        sem_signal(ctx->semid, 0);
    }
    return NULL;
}

// Συνάρτηση που αποσυνδέει τη διεργασία από την κοινή μνήμη
void cleanup_ipc(process_data_t *ctx) {
    if (ctx->shm_ptr) {
        shmdt(ctx->shm_ptr);
        ctx->shm_ptr = NULL;
    }
}
