#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>

#define BUFSIZE 20
#define P(x) sem_wait(x)
#define V(x) sem_post(x)

struct ThreadParams {
    char* fileName;
    char* buf;
    FILE* file;
    int* count;
    int sleep;
    sem_t* mutex;
    sem_t* empty;
    sem_t* full;

};

void *fill(void* context) {
    struct ThreadParams* params = context;

    char* line;
    size_t len = 0;
    ssize_t read;
    int restore = 0; /* if getline but space not enought, changed to drain and back , restore will be ture*/

    while (1) {
        usleep(params->sleep);
        P(params->empty);
        P(params->mutex);
        /* if restore */
        if (restore) {
            restore = 0;
            if (BUFSIZE - *params->count > read) {
                strncpy(&params->buf[*params->count], line, strlen(line) + 1);
                *params->count += read + 1;

                printf("fill thread: wrote [%s] into buffer (nwritten = %zu)\n", line, read + 1);
            } else {
                printf("fill thread: can not write [%s] -- not enough space (%zu)\n", line, read + 1);
                restore = 1;
            }
        }
        while ((read = getline(&line, &len, params->file)) != -1) {
            usleep(params->sleep);
            /* if buf is enough for this line*/

            if (BUFSIZE - *params->count > read) {
                strncpy(&params->buf[*params->count], line, strlen(line) + 1);
                *params->count += read + 1;

                printf("fill thread: wrote [%s] into buffer (nwritten = %zu)\n", line, read + 1);
            } else {
                printf("fill thread: can not write [%s] -- not enough space (%zu)\n", line, read + 1);
                restore = 1;
                break;
            }
        }
        if (-1 == read && BUFSIZE - *params->count > 5) {
            char* end = "QUIT";
            strncpy(&params->buf[*params->count], end, 5);
            *params->count += 5;
            printf("fill thread: wrote [QUIT] into buffer (nwritten = 5)\n");
            fclose(params->file);
            V(params->mutex);

            return 0;
        }
        V(params->mutex);
    }

    return 0;
}

void *drain(void* context) {
    struct ThreadParams* params = context;

    while (1) {
        usleep(params->sleep);
        sem_wait(params->mutex);
        while (*params->count != 0) {
            usleep(params->sleep);
            char* line = params->buf;
            /* check QUIT */
            if (0 == strcmp(line, "QUIT")) {
                printf("drain thread: read [QUIT] from buffer (nread = 5)\n");
                fclose(params->file);
                return 0;
            }

            /* get length of a line */
            fwrite(line, sizeof(char), strlen(line), params->file);
            printf("drain thread: read [%s] from buffer (nread = %zu)\n", line, strlen(line) + 1);

            /* move buffer */
            *params->count = *params->count - strlen(line) - 1;
            memcpy(params->buf, &params->buf[strlen(line) + 1], *params->count);

        }
        printf("drain thread: no new string in buffer\n");
        sem_post(params->mutex);
    }
    return 0;
}

int main(int argc, char* argv[])
{
    FILE* inputFile, * outputFile;
    char buf[BUFSIZE];
    int count = 0;
    ssize_t line;
    sem_t mutex, empty, full;
    pthread_t thread1, thread2;
    int iret1, iret2;

    struct ThreadParams* fillParams = (struct ThreadParams*) malloc(sizeof(struct ThreadParams));
    struct ThreadParams* drainParams = (struct ThreadParams*) malloc(sizeof(struct ThreadParams));

    fillParams->fileName = argv[1];
    fillParams->sleep = atoi(argv[3]);
    fillParams->buf = buf;
    fillParams->mutex = &mutex;
    fillParams->count = &count;

    drainParams->fileName = argv[2];
    drainParams->sleep = atoi(argv[4]);
    drainParams->buf = buf;
    drainParams->mutex = &mutex;
    drainParams->count = &count;

    /* open file */
    inputFile = fopen(fillParams->fileName, "r");
    if (NULL == inputFile) {
        fprintf(stderr, "open inputFile: %s\n", strerror(errno));
        exit(1);
    }

    outputFile = fopen(drainParams->fileName, "w+");
    if (NULL == outputFile) {
        fprintf(stderr, "create outputFile: %s\n", strerror(errno));
        exit(1);
    }

    /* init semaphore */
    if( sem_init(&mutex, 0, 1) < 0) {
        fprintf(stderr, "semaphore initilization: %s\n", strerror(errno));
        exit(1);
    }

    if( sem_init(&full, 0, 0) < 0) {
        fprintf(stderr, "semaphore initilization: %s\n", strerror(errno));
        exit(1);
    }

    if( sem_init(&empty, 0, 0) < 0) {
        fprintf(stderr, "semaphore initilization: %s\n", strerror(errno));
        exit(1);
    }

    fillParams->file = inputFile;
    fillParams->full = &full;
    fillParams->empty = &empty;
    drainParams->file = outputFile;
    drainParams->full = &full;
    drainParams->empty = &empty;


    printf("buffer size = %i\n", BUFSIZE);

    /* Create independent threads each of which will execute function */
    iret1 = pthread_create( &thread1, NULL, fill, fillParams);
    if(iret1)
    {
        fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
        exit(EXIT_FAILURE);
    }

    iret2 = pthread_create( &thread2, NULL, drain, drainParams);
    if(iret2)
    {
        fprintf(stderr,"Error - pthread_create() return code: %d\n",iret2);
        exit(EXIT_FAILURE);
    }

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    /* declear and release */
    sem_destroy(&mutex);
    return 0;
}
