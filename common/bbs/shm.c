#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "cmsys.h"
#include "cmbbs.h"
#include "common.h"
#include "var.h"

#ifdef USE_POSIX_SHM
static const bool use_posix_shm = true;
#else
static const bool use_posix_shm = false;
#endif

static SHM_t *
posix_open_shm(const char *path, int *is_created)
{
    void *shmptr = NULL;
    int is_new = 0;
    int fd = -1;
    int create = !!is_created;

    if (create) {
        fd = shm_open(path, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd >= 0) {
            is_new = 1;
        } else if (errno == EEXIST) {
            fd = shm_open(path, O_RDWR, 0666);
        }
        if (fd < 0) {
            fprintf(stderr, "[shm_open error] name = %s, errno = %d: %s\n", path, errno, strerror(errno));
            return NULL;
        }
        if (is_new) {
            if (ftruncate(fd, sizeof(SHM_t)) < 0) {
                fprintf(stderr, "[ftruncate error] name = %s, errno = %d: %s\n", path, errno, strerror(errno));
                close(fd);
                return NULL;
            }
        }
    } else {
        fd = shm_open(path, O_RDWR, 0666);
        if (fd < 0) {
            fprintf(stderr, "[shm_open error] name = %s, errno = %d: %s\n", path, errno, strerror(errno));
            return NULL;
        }
    }

    if (is_created) {
        *is_created = is_new;
    }

    shmptr = mmap(NULL, sizeof(SHM_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (shmptr == MAP_FAILED) {
        fprintf(stderr, "[mmap error] name = %s, errno = %d: %s\n", path, errno, strerror(errno));
        return NULL;
    }
    return (SHM_t *)shmptr;
}

static void
posix_shm_error(void)
{
    fprintf(stderr,
            "Shared Memory [name: %s, version: %d] ERROR!!\n"
            "System is not initialized. Run initbbs first.\n",
            SHM_NAME, SHM_VERSION);
}

static SHM_t *
sysv_open_shm(int shmkey, int *is_created)
{
    void *shmptr = NULL;
    int shmid = -1;
    int is_new = 0;
    int create = !!is_created;

    int flags = 0;

#ifdef USE_HUGETLB
    flags |= SHM_HUGETLB;
#endif

    if (create) {
        flags |= 0600 | IPC_CREAT;
        shmid = shmget(shmkey, sizeof(SHM_t), flags | IPC_EXCL);
        if (shmid >= 0) {
            is_new = 1;
        } else if (errno == EEXIST) {
            shmid = shmget(shmkey, sizeof(SHM_t), flags);
        }
    } else {
        shmid = shmget(shmkey, sizeof(SHM_t), flags);
    }

    if (shmid < 0) {
        fprintf(stderr, "[shmget error] key = 0x%x (%d), size = %zu, errno = %d: %s\n",
                shmkey, shmkey, sizeof(SHM_t), errno, strerror(errno));
        return NULL;
    }

    if (is_created) {
        *is_created = is_new;
    }

    shmptr = shmat(shmid, NULL, 0);
    if (shmptr == (void *)-1) {
        fprintf(stderr, "[shmat error] key = 0x%x (%d), shmid = %d, errno = %d: %s\n",
                shmkey, shmkey, shmid, errno, strerror(errno));
        return NULL;
    }
    return (SHM_t *)shmptr;
}

static void
sysv_shm_error(void)
{
    fprintf(stderr,
            "Shared Memory [key: 0x%x (%d), version: %d] ERROR!!\n"
            "System is not initialized. Run initbbs first.\n",
            SHM_KEY, SHM_KEY, SHM_VERSION);
}

static void
shm_error(void)
{
    if (use_posix_shm) {
        posix_shm_error();
        return;
    }
    sysv_shm_error();
}

SHM_t *
create_shm(int *is_created)
{
    if (use_posix_shm)
        return posix_open_shm(SHM_NAME, is_created);
    return sysv_open_shm(SHM_KEY, is_created);
}

SHM_t *
attach_shm(void)
{
    return create_shm(NULL);
}

SHM_t *
attach_check_SHM(void)
{
    SHM = attach_shm();
    if (!SHM) {
        return NULL;
    }

    if (SHM->version != SHM_VERSION || SHM->size != sizeof(SHM_t) || !SHM->loaded) {
        return NULL;
    }

    if (SHM->Btouchtime == 0) {
        SHM->Btouchtime = 1;
    }
    bcache = SHM->bcache;

    if (SHM->Ptouchtime == 0) {
        SHM->Ptouchtime = 1;
    }

    if (SHM->Ftouchtime == 0) {
        SHM->Ftouchtime = 1;
    }

    return SHM;
}

void
attach_SHM(void)
{
    if (!attach_check_SHM()) {
        shm_error();
        exit(1);
    }
}

