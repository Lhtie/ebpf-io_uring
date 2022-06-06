#include "liburing.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <aio.h>
#include <libaio.h>
#include <sys/sdt.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define QD          64
#define BS          (4 * 1024)
#define RANDCHAR    (rand()%95 + 32)

int infd, outfd;
off_t fsize, offset;

int get_filesize(int fd){
    struct stat st;
    if (fstat(fd, &st) < 0)
        return -1;
    fsize = st.st_size;
    return 0;
}

int test_sync(){
    int res;
    void *buf;

    
    for (int i = 0; i < QD; ++i){
        offset = rand() % ((fsize - BS) / BS) * BS;
        lseek(infd, offset, SEEK_SET);
        res = posix_memalign(&buf, BS, BS);
        DTRACE_PROBE("lhtie", read_request);
        res = read(infd, buf, BS);
        DTRACE_PROBE("lhtie", read_receive);
        free(buf);
    }

    for (int i = 0; i < QD; ++i){    
        res = posix_memalign(&buf, BS, BS);
        memset(buf, RANDCHAR, BS);
        DTRACE_PROBE("lhtie", write_request);
        res = write(outfd, buf, BS);
        DTRACE_PROBE("lhtie", write_receive);
        free(buf);
    }

    return 0;
}

struct io_data {
    size_t offset;
    bool isRead;
    struct iovec iov;
};

int test_io_uring(){
    struct io_uring ring;
    if (io_uring_queue_init(QD * 2, &ring, IORING_SETUP_SQPOLL) < 0){
        perror("initialize io_uring queue error ");
        return -1;
    }

    int res;
    off_t offset = 0;
    for (int i = 0; i < QD; ++i){
        struct io_uring_sqe *sqe;
        struct io_data *data;
        void *ptr;

        res = posix_memalign(&ptr, BS, BS + sizeof(*data));
        data = ptr + BS;
        data->offset = rand() % ((fsize - BS) / BS) * BS;
        data->iov.iov_base = ptr;
        data->iov.iov_len = BS;
        data->isRead = true;
        sqe = io_uring_get_sqe(&ring);
        DTRACE_PROBE("lhtie", read_request);
        io_uring_prep_readv(sqe, infd, &data->iov, 1, data->offset);
        io_uring_sqe_set_data(sqe, data);

        res = posix_memalign(&ptr, BS, BS + sizeof(*data));
        memset(ptr, RANDCHAR, BS);
        data = ptr + BS;
        data->offset = offset;
        offset += BS;
        data->iov.iov_base = ptr;
        data->iov.iov_len = BS;
        data->isRead = false;
        sqe = io_uring_get_sqe(&ring);
        DTRACE_PROBE("lhtie", write_request);
        io_uring_prep_writev(sqe, outfd, &data->iov, 1, data->offset);
        io_uring_sqe_set_data(sqe, data);
    }

    io_uring_submit(&ring);

    for (int i = 0; i < 2 * QD; ++i){
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        struct io_data *data = io_uring_cqe_get_data(cqe);
        void *ptr = (void *) data - data->iov.iov_len;
        if (data->isRead)
            DTRACE_PROBE("lhtie", read_receive);
        else DTRACE_PROBE("lhtie", write_receive);
        free(ptr);
        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
    return 0;
}

pthread_mutex_t mutex;
int cnt = 0;

void posix_aio_callback(__sigval_t sigval) {
    struct aiocb *aiocbp = (struct aiocb *)(sigval.sival_ptr);
    if (aiocbp->aio_fildes == infd)
        DTRACE_PROBE("lhtie", read_receive);
    else DTRACE_PROBE("lhtie", write_receive);
    pthread_mutex_lock(&mutex);
    cnt++;
    pthread_mutex_unlock(&mutex);
    free((void *)aiocbp->aio_buf);
    free(aiocbp);
}
int test_posix_aio(){
    struct aiocb *aiocbp;
    void* buf;
    off_t offset = 0;
    int res;

    for (int i = 0; i < QD; ++i){
        aiocbp = (struct aiocb*) malloc(sizeof(struct aiocb));
        memset(aiocbp, 0, sizeof(struct aiocb));
        res = posix_memalign(&buf, BS, BS);
        aiocbp->aio_fildes = infd;
        aiocbp->aio_buf = buf;
        aiocbp->aio_nbytes = BS;
        aiocbp->aio_offset = rand() % ((fsize - BS) / BS) * BS;
        aiocbp->aio_sigevent.sigev_notify = SIGEV_THREAD;
        aiocbp->aio_sigevent.sigev_notify_function = posix_aio_callback;
        aiocbp->aio_sigevent.sigev_notify_attributes = NULL;
        aiocbp->aio_sigevent.sigev_value.sival_ptr = aiocbp;
        DTRACE_PROBE("lhtie", read_request);
        res = aio_read(aiocbp);
        
        aiocbp = (struct aiocb*) malloc(sizeof(struct aiocb));
        memset(aiocbp, 0, sizeof(struct aiocb));
        res = posix_memalign(&buf, BS, BS);
        memset(buf, RANDCHAR, BS);
        aiocbp->aio_fildes = outfd;
        aiocbp->aio_buf = buf;
        aiocbp->aio_nbytes = BS;
        aiocbp->aio_offset = offset;
        offset += BS;
        aiocbp->aio_sigevent.sigev_notify = SIGEV_THREAD;
        aiocbp->aio_sigevent.sigev_notify_function = posix_aio_callback;
        aiocbp->aio_sigevent.sigev_notify_attributes = NULL;
        aiocbp->aio_sigevent.sigev_value.sival_ptr = aiocbp;
        DTRACE_PROBE("lhtie", write_request);
        res = aio_write(aiocbp);
    }

    while (true) {
        pthread_mutex_lock(&mutex);
        if (cnt >= 2 * QD){
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
    }
    return 0;
}

io_context_t ctx;

int test_libaio(){
    if (io_setup(2 * QD, &ctx) < 0){
        perror("setup aio error ");
        return -1;
    }

    struct iocb **iocbs = calloc(QD * 2, sizeof(struct iocb*));
    void *buf;
    off_t offset = 0;
    int res;

    for (int i = 0; i < QD; ++i){
        res = posix_memalign(&buf, BS, BS);
        iocbs[2*i] = (struct iocb *)malloc(sizeof(struct iocb));
        DTRACE_PROBE("lhtie", read_request);
        io_prep_pread(iocbs[2*i], infd, buf, BS, rand() % ((fsize - BS) / BS) * BS);
            
        res = posix_memalign(&buf, BS, BS);
        memset(buf, RANDCHAR, BS);
        iocbs[2*i+1] = (struct iocb *)malloc(sizeof(struct iocb));
        DTRACE_PROBE("lhtie", write_request);
        io_prep_pwrite(iocbs[2*i+1], outfd, buf, BS, offset);
        offset += BS;
    }
    
    io_submit(ctx, 2 * QD, iocbs);

    int cnt = 0;
    while (cnt < 2 * QD){
        struct io_event *events = calloc(2 * QD, sizeof(struct io_event));
        int res = io_getevents(ctx, 1, 2 * QD, events, NULL);
        for (int i = 0; i < res; ++i){
            struct iocb *obj = events[i].obj;
            if (obj->aio_fildes == infd)
                DTRACE_PROBE("lhtie", read_receive);
            else DTRACE_PROBE("lhtie", write_receive);
            free(obj->u.c.buf);
            free(obj);
        }
        cnt += res;
        free(events);
    }

    free(iocbs);
    return 0;
}

/*
gcc testIOLatency.c -o testIOLatency -luring -laio -lrt -Wall -O2 -D_GNU_SOURCE
*/
int main(int argc, char *argv[]){
    srand(time(NULL));
    if (argc < 4){
        perror("missing args ");
        return -1;
    }

    char *engine = argv[1];
    infd = open(argv[2], O_RDONLY | O_DIRECT);
    outfd = open(argv[3], O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (infd < 0 || outfd < 0){
        perror("fail to open file ");
        return -1;
    }
    if (get_filesize(infd) < 0){
        perror("fail to get state of file ");
        return -1;
    }

    if (strcmp(engine, "sync") == 0){
        if (test_sync() < 0){
            perror("test sync error ");
            return -1;
        }
    } else if (strcmp(engine, "io_uring") == 0){
        if (test_io_uring() < 0){
            perror("test io uring error ");
            return -1;
        }
    } else if (strcmp(engine, "posix_aio") == 0){
        if (test_posix_aio() < 0){
            perror("test posix aio error ");
            return -1;
        }
    } else {
        if (test_libaio() < 0){
            perror("test libaio error ");
            return -1;
        }
    }

    close(infd), close(outfd);
    return 0;
}