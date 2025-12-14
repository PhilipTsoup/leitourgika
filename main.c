#include "app.h"

int main(int argc, char *argv[]) {

    if (argc < 2) {
        // fprintf(stderr, "Χρήση: %s <dialogue_id>\n", argv[0]);
        fprintf(stderr, "Δώστε 0 για να δημιουργήσετε νέο διάλογο.\n");
        return 1;
    }
    
    // Αρχικοποίηση δεδομένων της διεργασίας
    process_data_t context;
    memset(&context, 0, sizeof(process_data_t));
    context.is_running = 1;
    pthread_mutex_init(&context.local_mutex, NULL);

    context.my_dialogue_id = atoi(argv[1]);

    // Συνάρτηση αρχικοποίησης
    if (init_ipc(&context) == -1) {
        return 1;
    }
    
    
    // Συνάρτηση εισαγωγής της διεργασίας στο διάλογο
    if (join_dialogue(&context) == -1) {
        return 1; 
    }
    
    printf("Διεργασία PID %d: Συνδέθηκε στον Διάλογο ID %d με %d συμμετέχοντες.\n", 
           getpid(), context.my_dialogue_id, 
           context.shm_ptr->dialogues[context.my_dialogue_index].num_participants);
    
    // Δημιουργία νήματος για αποστολή μηνυμάτων
    if (pthread_create(&context.sender_tid, NULL, sender_thread, &context) != 0) {
        perror("pthread_create (sender)");
        cleanup_ipc(&context);
        return 1;
    }

    // Δημιουργία νήματος για ανάγνωση μηνυμάτων
    if (pthread_create(&context.receiver_tid, NULL, receiver_thread, &context) != 0) {
        perror("pthread_create (receiver)");
        // Πρέπει να στείλουμε σήμα τερματισμού στο sender_thread αν αποτύχει αυτό
        context.is_running = 0; 
        pthread_join(context.sender_tid, NULL); // Περιμένει το sender να τελειώσει
        cleanup_ipc(&context);
        return 1;
    }

    // Το κύριο νήμα περιμένει μέχρι να τερματίσουν τα νήματα αποστολής/λήψης έπειτα από TERMINATE
    pthread_join(context.sender_tid, NULL);
    pthread_join(context.receiver_tid, NULL);

    printf("Διεργασία PID %d: Όλα τα νήματα τερμάτισαν.\n", getpid());
    
    // Καθαρισμός ipc/ Καταστροφή mutex
    cleanup_ipc(&context);
    pthread_mutex_destroy(&context.local_mutex);

    return 0;
}       
