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
#define MAXSIZE     (128 * 1024 * 1024)
#define ts          10          // seconds
#define RANDCHAR    (rand()%95 + 32)

#define mode_t  unsigned short
#define rRD     0
#define sRD     1
#define rWR     2
#define sWR     3

mode_t mode;
int fd;
off_t fsize, offset;
size_t cnt = 0;

struct timeval t_begin, t_end;
double rem_time;

int open_file(char *file){
    if (mode == rRD || mode == sRD)
        return open(file, O_RDONLY | O_DIRECT);
    else return open(file, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
}
int get_filesize(){
    struct stat st;
    if (fstat(fd, &st) < 0)
        return -1;
    fsize = st.st_size;
    return 0;
}
off_t get_offset(off_t size){
    off_t off = 0;
    switch (mode){
        case rRD:
            return rand() % ((fsize - size) / size) * size;
        case sRD:
            if (offset + size > fsize)
                offset = 0;
            off = offset;
            offset += size;
            return off;
        case rWR:
            return rand() % ((MAXSIZE - size) / size) * size;      
        case sWR:
            off = offset;
            offset += size;
            return off;
    }
    return off;
}

void run_sync(){
    off_t off = 0, size = BS;
    void *buf;
    int res = posix_memalign(&buf, size, size);

    switch (mode){
        case rRD:
            off = rand() % ((fsize - size) / size) * size;
            lseek(fd, off, SEEK_SET);
            res = read(fd, buf, size);
            break;
        case sRD:
            if (offset + size > fsize){
                offset = 0;
                lseek(fd, 0, SEEK_SET);
            }
            offset += size;
            res = read(fd, buf, size);
            break;
        case rWR:
            off = rand() % ((MAXSIZE - size) / size) * size;
            lseek(fd, off, SEEK_SET);
            memset(buf, RANDCHAR, size);
            res = write(fd, buf, size);
            break;
        case sWR:
            offset += size;
            memset(buf, RANDCHAR, size);
            res = write(fd, buf, size);
    }

    cnt++;
    free(buf);
}
int test_sync(){
    lseek(fd, 0, SEEK_SET);
    cnt = 0;

    char *shared;
	shared = (char *) mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *shared = 'Y';
    
    pid_t pid = fork();
    if (pid == 0){
        rem_time = ts * 1000000.0;
        gettimeofday(&t_begin, NULL);
        while (true){
            gettimeofday(&t_end, NULL);
            if (1000000.0 * (t_end.tv_sec - t_begin.tv_sec) + (t_end.tv_usec - t_begin.tv_usec) > rem_time){
                *shared = 'N';
                return 1;
            }
        }
    } else {
        offset = 0;
        while (true){
            if (*shared == 'N') 
                break;
            run_sync();
        }
    }

    printf("sync: ");
    switch (mode){
        case rRD: printf("random read "); break;
        case sRD: printf("sequential read "); break;
        case rWR: printf("random write "); break;
        case sWR: printf("sequential write"); break;
    }
    printf("operates with bs %d KB, IOPS: %ld /seconds\n", BS / 1024, cnt / ts);

    return 0;
}

struct io_data {
    size_t offset;
    struct iovec iov;
};
int inflight = 0;

void prepare_sqe(struct io_uring *ring, off_t size){
	struct io_uring_sqe *sqe;
	struct io_data *data;
	void *ptr;

	sqe = io_uring_get_sqe(ring);
    if (!sqe) return ;
    int res = posix_memalign(&ptr, size, size + sizeof(*data));
	data = ptr + size;
	data->offset = get_offset(size);
	data->iov.iov_base = ptr;
	data->iov.iov_len = size;
    if (mode == rRD || mode == sRD){
	    io_uring_prep_readv(sqe, fd, &data->iov, 1, data->offset);
    } else {
        memset(ptr, RANDCHAR, size);
        io_uring_prep_writev(sqe, fd, &data->iov, 1, data->offset);
    }
	io_uring_sqe_set_data(sqe, data);
}
void handle_cqe(struct io_uring *ring, struct io_uring_cqe *cqe){
	struct io_data *data = io_uring_cqe_get_data(cqe);
    void *ptr = (void *) data - data->iov.iov_len;
    free(ptr);
	io_uring_cqe_seen(ring, cqe);
    cnt++;
}
void run_io_uring(struct io_uring *ring){
    struct io_uring_cqe *cqe;
    int has_inflight = inflight;
	int depth = QD;

    while (inflight < QD) {
        prepare_sqe(ring, BS);
        inflight++;
    }

    if (has_inflight != inflight)
        io_uring_submit(ring);

    while (inflight >= depth) {
        io_uring_wait_cqe(ring, &cqe);
        handle_cqe(ring, cqe);
        inflight--;
    }
}
int test_io_uring(){
    struct io_uring ring;
    cnt = 0;
    if (io_uring_queue_init(QD, &ring, IORING_SETUP_SQPOLL) < 0){
        perror("initialize io_uring queue error ");
        return -1;
    }
    
    char *shared;
	shared = (char *) mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *shared = 'Y';
    
    pid_t pid = fork();
    if (pid == 0){
        rem_time = ts * 1000000.0;
        gettimeofday(&t_begin, NULL);
        while (true){
            gettimeofday(&t_end, NULL);
            if (1000000.0 * (t_end.tv_sec - t_begin.tv_sec) + (t_end.tv_usec - t_begin.tv_usec) > rem_time){
                *shared = 'N';
                return 1;
            }
        }
    } else {
        offset = 0;
        while (true){
            if (*shared == 'N')
                break;
            run_io_uring(&ring);
        }
    }

    printf("io_uring: ");
    switch (mode){
        case rRD: printf("random read "); break;
        case sRD: printf("sequential read "); break;
        case rWR: printf("random write "); break;
        case sWR: printf("sequential write"); break;
    }
    printf("operates with bs %d KB, IOPS: %ld /seconds\n", BS / 1024, cnt / ts);

    io_uring_queue_exit(&ring);
    return 0;
}

pthread_mutex_t mutex;
int inReq = 0;

void posix_aio_callback(__sigval_t sigval) {
    struct aiocb *aiocbp = (struct aiocb *)(sigval.sival_ptr);
    free((void *)aiocbp->aio_buf);
    free(aiocbp);
    pthread_mutex_lock(&mutex);
    inReq--;
    cnt++;
    pthread_mutex_unlock(&mutex);
}
void run_posix_aio(){
    struct aiocb *aiocbp;
    void* buf;
    off_t size = BS;

    while (inReq < QD){
        aiocbp = (struct aiocb*) malloc(sizeof(struct aiocb));
        memset(aiocbp, 0, sizeof(struct aiocb));
        int res = posix_memalign(&buf, size, size);
        aiocbp->aio_fildes = fd;
        aiocbp->aio_buf = buf;
        aiocbp->aio_nbytes = size;
        aiocbp->aio_offset = get_offset(size);
        aiocbp->aio_sigevent.sigev_notify = SIGEV_THREAD;
        aiocbp->aio_sigevent.sigev_notify_function = posix_aio_callback;
        aiocbp->aio_sigevent.sigev_notify_attributes = NULL;
        aiocbp->aio_sigevent.sigev_value.sival_ptr = aiocbp;

        pthread_mutex_lock(&mutex);
        if (inReq < QD){
            if (mode == rRD || mode == sRD){
                res = aio_read(aiocbp);
            } else {
                memset(buf, RANDCHAR, size);
                res = aio_write(aiocbp);
            }
            inReq++;
        } else {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
    }
}
int test_posix_aio(){
    cnt = 0;
    char *shared;
	shared = (char *) mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *shared = 'Y';
    
    pid_t pid = fork();
    if (pid == 0){
        rem_time = ts * 1000000.0;
        gettimeofday(&t_begin, NULL);
        while (true){
            gettimeofday(&t_end, NULL);
            if (1000000.0 * (t_end.tv_sec - t_begin.tv_sec) + (t_end.tv_usec - t_begin.tv_usec) > rem_time){
                *shared = 'N';
                return 1;
            }
        }
    } else {
        offset = 0;
        while (true){
            if (*shared == 'N')
                break;
            run_posix_aio();
        }
    }

    printf("posix_aio: ");
    switch (mode){
        case rRD: printf("random read "); break;
        case sRD: printf("sequential read "); break;
        case rWR: printf("random write "); break;
        case sWR: printf("sequential write"); break;
    }
    printf("operates with bs %d KB, IOPS: %ld /seconds\n", BS / 1024, cnt / ts);

    return 0;
}

io_context_t ctx;
int inProc = 0;

void run_libaio(){
    struct iocb **iocbs = calloc(QD - inProc, sizeof(struct iocb*));
    void *buf;
    off_t size = BS;
    int nr = 0;

    while (nr < QD - inProc){
        int res = posix_memalign(&buf, size, size);
        iocbs[nr] = (struct iocb *)malloc(sizeof(struct iocb));
        if (mode == rRD || mode == sRD){
            io_prep_pread(iocbs[nr], fd, buf, size, get_offset(size));
        } else {
            memset(buf, RANDCHAR, size);
            io_prep_pwrite(iocbs[nr], fd, buf, size, get_offset(size));
        }
        nr++;
    }
    if (nr > 0){
        inProc += io_submit(ctx, nr, iocbs);
    }

    struct io_event *events = calloc(QD, sizeof(struct io_event));
    int res = io_getevents(ctx, 1, QD, events, NULL);
    inProc -= res;
    for (int i = 0; i < res; ++i){
        struct iocb *obj = events[i].obj;
        free(obj->u.c.buf);
        free(obj);
    }

    cnt += res;
    free(iocbs);
    free(events);
}
int test_libaio(){
    cnt = 0;
    if (io_setup(QD, &ctx) < 0){
        perror("setup aio error ");
        return -1;
    }

    char *shared;
	shared = (char *) mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *shared = 'Y';
    
    pid_t pid = fork();
    if (pid == 0){
        rem_time = ts * 1000000.0;
        gettimeofday(&t_begin, NULL);
        while (true){
            gettimeofday(&t_end, NULL);
            if (1000000.0 * (t_end.tv_sec - t_begin.tv_sec) + (t_end.tv_usec - t_begin.tv_usec) > rem_time){
                *shared = 'N';
                return 1;
            }
        }
    } else {
        offset = 0;
        while (true){
            if (*shared == 'N')
                break;
            run_libaio();
        }
    }

    printf("linux libaio: ");
    switch (mode){
        case rRD: printf("random read "); break;
        case sRD: printf("sequential read "); break;
        case rWR: printf("random write "); break;
        case sWR: printf("sequential write"); break;
    }
    printf("operates with bs %d KB, IOPS: %ld /seconds\n", BS / 1024, cnt / ts);

    if (io_destroy(ctx) < 0){
        perror("fail to destroy aio ");
        return -1;
    }
    return 0;
}

/*
gcc testIOPS.c -o testIOPS -luring -laio -lrt -Wall -O2 -D_GNU_SOURCE
./testIOPS dict.txt -m=rRD       * random read
                      =sRD       * sequential read
                      =rWR       * random write
                      =sWR       * sequential write
*/
int main(int argc, char *argv[]){
    srand(time(NULL));
    int ret = 0;
    if (argc < 3){
        perror("missing args ");
        return -1;
    }
    char *mode_info = argv[2];
    if (strcmp(mode_info, "-m=rRD") == 0)
        mode = rRD;
    else if (strcmp(mode_info, "-m=sRD") == 0)
        mode = sRD;
    else if (strcmp(mode_info, "-m=rWR") == 0)
        mode = rWR;
    else if (strcmp(mode_info, "-m=sWR") == 0)
        mode = sWR;
    else {
        perror("mode error ");
        return -1;
    }

    fd = open_file(argv[1]);
    if (fd < 0){
        perror("fail to open file ");
        return -1;
    }
    if (get_filesize() < 0){
        perror("fail to get state of file ");
        return -1;
    }

    ret = test_sync();
    if (ret < 0){
        perror("fail to test sync ");
        return -1;
    } else if (ret == 1){ // ret from child process
        return 0;
    }
    ret = test_io_uring();
    if (ret < 0){
        perror("fail to test io uring ");
        return -1;
    } else if (ret == 1){
        return 0;
    }
    ret = test_posix_aio();
    if (ret < 0){
        perror("fail to test posix aio ");
        return -1;
    } else if (ret == 1){
        return 0;
    }
    ret = test_libaio();
    if (ret < 0){
        perror("fail to test libaio ");
        return -1;
    } else if (ret == 1){
        return 0;
    }

    close(fd);
    return 0;
}