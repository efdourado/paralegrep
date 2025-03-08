#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_PATH_LENGTH 1024
#define MAX_WORKER_THREADS 10
#define TOP_RANK 10
#define FILESET_DIR "fileset"

typedef struct {
    char filename[256];
    int count;
} FileData;

FileData ranking[TOP_RANK];
pthread_mutex_t ranking_mutex;
pthread_mutex_t worker_mutex;
pthread_cond_t worker_cond;

int active_workers = 0;

typedef struct {
    char filename[256];
    time_t last_mod_time;
} FileModTime;

FileModTime file_mod_times[MAX_PATH_LENGTH];
int file_mod_count = 0;

void upd_ranking(const char *filename, int count) {
    pthread_mutex_lock(&ranking_mutex);

    int found = 0;
    for (int i = 0; i < TOP_RANK; i++) {
        if (strcmp(ranking[i].filename, filename) == 0) {
            ranking[i].count = count;
            found = 1;
            break;
    } }

    if (!found) {
        for (int i = 0; i < TOP_RANK; i++) {
            if (ranking[i].count < count) {
                for (int j = TOP_RANK - 1; j > i; j--) {
                    ranking[j] = ranking[j - 1];
                }
                strcpy(ranking[i].filename, filename);
                ranking[i].count = count;
                break;
    } } }

    for (int i = 0; i < TOP_RANK - 1; i++) {
        for (int j = i + 1; j < TOP_RANK; j++) {
            if (ranking[j].count > ranking[i].count) {
                FileData temp = ranking[i];
                ranking[i] = ranking[j];
                ranking[j] = temp;
    } } }

    pthread_mutex_unlock(&ranking_mutex);
}

int count_word_occ(const char *filename, const char *word) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Erro ao abrir arq");
        return 0;
    }

    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), file)) {
        char *ptr = line;
        while ((ptr = strstr(ptr, word)) != NULL) {
            count++;
            ptr++;
    } }

    fclose(file);
    return count;
}

int should_process_file(const char *filename, time_t mod_time) {
    for (int i = 0; i < file_mod_count; i++) {
        if (strcmp(file_mod_times[i].filename, filename) == 0) {
            if (file_mod_times[i].last_mod_time < mod_time) {
                file_mod_times[i].last_mod_time = mod_time;
                return 1;
            }
            return 1;
    } }

    strcpy(file_mod_times[file_mod_count].filename, filename);
    file_mod_times[file_mod_count].last_mod_time = mod_time;
    file_mod_count++;
    return 1;
}

void *worker_thread(void *arg) {
    const char *word = ((const char **)arg)[0];
    const char *filename = ((const char **)arg)[1];

    int count = count_word_occ(filename, word);
    upd_ranking(filename, count);

    pthread_mutex_lock(&worker_mutex);
    active_workers--;
    pthread_cond_signal(&worker_cond);
    pthread_mutex_unlock(&worker_mutex);

    free(arg);
    return NULL;
}

void monitor_dir(const char *word) {
    struct stat file_stat;

    DIR *dir = opendir(FILESET_DIR);
    if (dir == NULL) {
        perror("Erro ao abrir diretório");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char filepath[MAX_PATH_LENGTH];
            snprintf(filepath, sizeof(filepath), "%s/%s", FILESET_DIR, entry->d_name);

            if (stat(filepath, &file_stat) == -1) {
                perror("Erro ao obter informações do arq");
                continue;
            }

            if (should_process_file(entry->d_name, file_stat.st_mtime)) {
                pthread_mutex_lock(&worker_mutex);
                while (active_workers >= MAX_WORKER_THREADS) {
                    pthread_cond_wait(&worker_cond, &worker_mutex);
                }

                active_workers++;
                pthread_mutex_unlock(&worker_mutex);

                char **args = malloc(2 * sizeof(char *));
                args[0] = strdup(word);
                args[1] = strdup(filepath);

                pthread_t worker;
                pthread_create(&worker, NULL, worker_thread, args);
                pthread_detach(worker);
    } } }

    closedir(dir);
}

void *ranking_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&ranking_mutex);

        printf("\ncurrent ranking:\n");

        for (int i = 0; i < TOP_RANK; i++) {
            if (ranking[i].count > 0) {
                printf("%d. %s - %d occurrences\n", i + 1, ranking[i].filename, ranking[i].count);
        } }

        pthread_mutex_unlock(&ranking_mutex);
        sleep(5);
    }

    return NULL;
}

void *dispatcher_thread(void *arg) {
    const char *word = (const char *)arg;
    while (1) {
        monitor_dir(word);
        sleep(5);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "uso: %s <palavra>\n", argv[0]);
        return 1;
    }

    const char *word = argv[1];
    printf("procurando por: %s\n", word);

    pthread_t ranking_thread_id;
    pthread_t dispatcher_thread_id;

    pthread_mutex_init(&ranking_mutex, NULL);
    pthread_mutex_init(&worker_mutex, NULL);
    pthread_cond_init(&worker_cond, NULL);

    for (int i = 0; i < TOP_RANK; i++) {
        ranking[i].count = 0;
        strcpy(ranking[i].filename, "");
    }

    pthread_create(&ranking_thread_id, NULL, ranking_thread, NULL);
    pthread_create(&dispatcher_thread_id, NULL, dispatcher_thread, (void *)word);

    pthread_join(dispatcher_thread_id, NULL);
    pthread_join(ranking_thread_id, NULL);

    pthread_mutex_destroy(&ranking_mutex);
    pthread_mutex_destroy(&worker_mutex);
    pthread_cond_destroy(&worker_cond);

    return 0;
}