/* TeXstudio Espresso WSL pipe/process supervisor. GPL-3.0-only.
 * The editor owns stdin. Engine stdout/stderr pass through unchanged, including
 * binary PNG bodies. EOF, a closed output pipe or frontend exit releases the
 * isolated engine process group, including forked compiler checkpoints. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int signal_number) { (void)signal_number; stopped = 1; }
int main(int argc, char **argv)
{
    if (argc < 4 || strcmp(argv[2], "--")) {
        fprintf(stderr, "Usage: texpresso-wsl WORKING-DIRECTORY -- ENGINE [ARGUMENTS...]\n");
        return 2;
    }
    int input[2], ready[2];
    if (pipe(input) || pipe(ready)) { perror("[wsl] pipe"); return 1; }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, stop); signal(SIGINT, stop); signal(SIGHUP, stop);
    pid_t child = fork();
    if (child < 0) { perror("[wsl] fork"); return 1; }
    if (!child) {
        close(input[1]); close(ready[0]);
        signal(SIGPIPE, SIG_DFL); signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL); signal(SIGHUP, SIG_DFL);
        if (setsid() < 0 || chdir(argv[1]) || dup2(input[0], STDIN_FILENO) < 0) {
            perror("[wsl] starting engine session"); _exit(127);
        }
        close(input[0]);
        if (write(ready[1], "R", 1) != 1) _exit(127);
        close(ready[1]);
        execv(argv[3], argv + 3);
        perror("[wsl] launching engine"); _exit(127);
    }
    close(input[0]); close(ready[1]);
    char initialized;
    const ssize_t initialized_bytes = read(ready[0], &initialized, 1);
    close(ready[0]);
    int status = 0, exited = 0;
    if (initialized_bytes == 1) {
        int flags = fcntl(input[1], F_GETFL);
        if (flags < 0 || fcntl(input[1], F_SETFL, flags | O_NONBLOCK) < 0) stopped = 1;
        char buffer[65536]; size_t size = 0, offset = 0;
        while (!stopped) {
            pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child) { exited = 1; break; }
            if (result < 0 && errno != EINTR) break;
            struct pollfd descriptors[] = {
                {STDIN_FILENO, size ? 0 : POLLIN, 0},
                {input[1], size ? POLLOUT : 0, 0}
            };
            int count = poll(descriptors, 2, 100);
            if (count < 0) { if (errno == EINTR) continue; break; }
            if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            if (descriptors[0].revents & POLLIN) {
                if (!size) {
                    ssize_t bytes = read(STDIN_FILENO, buffer, sizeof buffer);
                    if (bytes == 0) break;
                    if (bytes < 0) { if (errno == EINTR || errno == EAGAIN) continue; break; }
                    size = (size_t)bytes; offset = 0;
                }
            }
            if (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            if (size && descriptors[1].revents & POLLOUT) {
                ssize_t bytes = write(input[1], buffer + offset, size - offset);
                if (bytes < 0) { if (errno == EINTR || errno == EAGAIN) continue; break; }
                offset += (size_t)bytes;
                if (offset == size) size = offset = 0;
            }
        }
        /* ready guarantees this group belongs to our live child. */
        kill(-child, SIGTERM);
        const struct timespec grace = {.tv_sec = 0, .tv_nsec = 150000000};
        nanosleep(&grace, NULL);
        kill(-child, SIGKILL);
    }
    close(input[1]);
    if (!exited) while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
