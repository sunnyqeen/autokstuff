#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/time.h>

#define APP_VERSION "1.0"
#define PAYLOAD_NAME "autokstuff.elf"

typedef struct notify_request {
    char unused[45];
    char message[3075];
} notify_request_t;

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
} app_info_t;

extern int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);
extern int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
extern int kstuff_toggle(int option, int enable);

static void notify(const char* fmt, ...) {
    notify_request_t req = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message) - 1, fmt, args);
    va_end(args);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// from john-tornblom
static pid_t find_pid(const char *name) {
    int mib[4] = {1, 14, 8, 0};
    pid_t mypid = getpid();
    pid_t pid = -1;
    size_t buf_size;
    uint8_t *buf;

    if (sysctl(mib, 4, 0, &buf_size, 0, 0)) {
        return -1;
    }

    if (!(buf = malloc(buf_size))) {
        return -1;
    }

    if (sysctl(mib, 4, buf, &buf_size, 0, 0)) {
        free(buf);
        return -1;
    }

    for (uint8_t *ptr = buf; ptr < (buf + buf_size);) {
        int ki_structsize = *(int *)ptr;
        pid_t ki_pid = *(pid_t *)&ptr[72];
        char *ki_tdname = (char *)&ptr[447];

        ptr += ki_structsize;
        if (!strcmp(name, ki_tdname) && ki_pid != mypid) {
            pid = ki_pid;
        }
    }

    free(buf);

    return pid;
}

static void process_game(int option, pid_t pid, int delay) {
    int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return;
    }

    struct kevent kev;
    EV_SET(&kev, pid, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_CLEAR, NOTE_EXIT, 0, NULL);

    int ret = kevent(kq, &kev, 1, NULL, 0, NULL);
    if (ret == -1) {
        printf("[WARNING] kevent registration failed for pid %d: %s\n", pid, strerror(errno));
        close(kq);
        sleep(3);
        return;
    }

    printf("[INFO] Waiting for pid %d to exit...\n", pid);

    int counter = 0;
    while (1) {
        struct kevent event;
        struct timespec tout = {1, 0};
        int nev = kevent(kq, NULL, 0, &event, 1,  &tout);

        if (nev < 0) {
            printf("[WARNING] kevent wait failed: %s\n", strerror(errno));
            close(kq);
            return;
        }

        if (nev > 0 && event.fflags & NOTE_EXIT) {
            printf("[INFO] Process %d exited\n", pid);
            break;
        }

        if (nev == 0) {
          if(counter >= 0 && ++counter == delay) {
            kstuff_toggle(option, 0);
            counter = -1;
          }
        }
    }

    close(kq);
    kstuff_toggle(option, 1);
}

static int find_highest_sandbox_number(const char* title_id) {
    char base_path[PATH_MAX];
    int highest = -1;

    for (int i = 0; i < 1000; i++) {
        snprintf(base_path, sizeof(base_path), "/mnt/sandbox/%s_%03d", title_id, i);

        struct stat st;
        if (stat(base_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            highest = i;
        } else {
            break;
        }
    }

    return highest;
}

static void kstuff_toggle_game(int option, pid_t child_pid, const char *title_id) {
    // Find highest sandbox number
    int sandbox_num = find_highest_sandbox_number(title_id);
    if (sandbox_num == -1) {
        printf("[WARNING] No sandbox found for %s\n", title_id);
        return;
    }

    // Build sandbox_id
    char sandbox_id[14];
    snprintf(sandbox_id, sizeof(sandbox_id), "%s_%03d", title_id, sandbox_num);

    // Check if autokstuff exists
    char autokstuff_src_path[PATH_MAX];
    snprintf(autokstuff_src_path, sizeof(autokstuff_src_path), "/mnt/sandbox/%s/app0/autokstuff", sandbox_id);
    struct stat st;
    if (stat(autokstuff_src_path, &st) != 0) {
        snprintf(autokstuff_src_path, sizeof(autokstuff_src_path), "/data/autokstuff/%s", title_id);
        if (stat(autokstuff_src_path, &st) != 0) {
            return;
        }
    }

    int delay = 0;
    int fd = open(autokstuff_src_path, O_RDONLY);
    if (fd != -1) {
      #define READ_SIZE_MAX 32
      char buf[READ_SIZE_MAX];
      int ret = read(fd, buf, READ_SIZE_MAX - 1);
      buf[ret > 0 ? ret : 0] = 0;
      delay = atoi(buf);
      close(fd);
    }
    if (delay <= 0) {
        return;
    }

    notify("%s is running, auto disable kstuff in %ds...", title_id, delay);
    printf("[INFO] %s is running (pid %d) in sandbox %s, auto disable kstuff in %ds...\n", title_id, child_pid, sandbox_id, delay);

    process_game(option, child_pid, delay);
}

int main() {
    syscall(SYS_thr_set_name, -1, PAYLOAD_NAME);

    int pid;
    while ((pid = find_pid(PAYLOAD_NAME)) > 0) {
        if (kill(pid, SIGKILL)) {
            return -1;
        }
        printf("[INFO] Killed old instance\n");
        sleep(1);
    }

    pid_t syscore_pid = find_pid("SceSysCore.elf");
    if (syscore_pid == -1) {
        printf("[WARNING] Failed to find SceSysCore.elf pid\n");
        return -1;
    }

    int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return -1;
    }

    struct kevent kev;
    EV_SET(&kev, syscore_pid, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_FORK | NOTE_EXEC | NOTE_TRACK, 0, NULL);

    int ret = kevent(kq, &kev, 1, NULL, 0, NULL);
    if (ret == -1) {
        perror("kevent");
        close(kq);
        return -1;
    }
	
	notify("autokstuff v" APP_VERSION " By SunnyQeen");
    printf("[INFO] Monitoring SceSysCore.elf (pid %d) for game launches...\n", syscore_pid);

    pid_t child_pid = -1;

    while (1) {
        struct kevent event;
        int nev = kevent(kq, NULL, 0, &event, 1, NULL);

        if (nev < 0) {
            perror("kevent");
            continue;
        }

        if (nev == 0) continue;

        if (event.fflags & NOTE_CHILD) {
            child_pid = event.ident;
        }

        if (event.fflags & NOTE_EXEC && child_pid != -1 && event.ident == child_pid) {
            app_info_t appinfo = {0};
            if (sceKernelGetAppInfo(child_pid, &appinfo) != 0) {
                child_pid = -1;
                continue;
            }

            char title_id[10] = {0};
            memcpy(title_id, appinfo.title_id, 9);

            // Check if it's a PPSA/CUSA game
            int option = strncmp(title_id, "PPSA", 4) == 0 ? 1 : (strncmp(title_id, "CUSA", 4) == 0 ? 2 : 0);
            if (!option) {
                child_pid = -1;
                continue;
            }

            kstuff_toggle_game(option, child_pid, title_id);
            child_pid = -1;
        }
    }

    close(kq);
    return 0;
}

