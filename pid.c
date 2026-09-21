#define _GNU_SOURCE

#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <net/if.h>


int remove_cgroup(const char *cgroup_dir)
{
    int rmdir_result = rmdir(cgroup_dir);
    if (rmdir_result != 0)
    {
        perror("rmdir");
        return rmdir_result;
    }
    return 0;
}

int directory_exists(const char *path)
{
    struct stat stats;

    // stat returns 0 if the path exists
    if (stat(path, &stats) == 0)
    {
        // Check if the valid path is actually a directory
        return S_ISDIR(stats.st_mode);
    }

    return 0; // Directory does not exist or is inaccessible
}

int check_cgroup_exists(const char *cgroup_base)
{
    return directory_exists(cgroup_base);
}

int create_cgroup(const char *cgroup_base)
{
    int mkdir_result = mkdir(cgroup_base, 0700);
    if (mkdir_result != 0)
    {
        perror("mkdir");
        return mkdir_result;
    }
    return 0;
}

int smart_snprintf(char *buffer, size_t buffer_size, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    int needed = vsnprintf(buffer, buffer_size, format, args);
    va_end(args);

    if (needed < 0)
    {
        perror("vsnprintf");
        return -1;
    }

    return needed;
}

int open_and_write_to_subtree_control(const char *cgroup_base)
{
    if (!check_cgroup_exists(cgroup_base))
    {
        return -1;
    }

    const char *text = "+cpu +memory\n";
    char filename[512];

    int snprintf_result = smart_snprintf(filename, sizeof(filename), "%s/cgroup.subtree_control", cgroup_base);
    if (snprintf_result < 0 || (size_t)snprintf_result >= sizeof(filename))
    {
        fprintf(stderr, "cgroup.subtree_control path is too long\n");
        return -1;
    }

    int subtree_control_fd = open(filename, O_WRONLY);
    if (subtree_control_fd < 0)
    {
        perror("open subtree control");
        return -1;
    }

    int write_result = write(subtree_control_fd, text, strlen(text));
    if (write_result != strlen(text))
    {
        perror("write to subtree control");
        close(subtree_control_fd);
        return -1;
    }

    close(subtree_control_fd);
    return 0;
}

int prep_cgroup(const char *cgroup_base)
{
    if (!check_cgroup_exists(cgroup_base))
    {
        if (create_cgroup(cgroup_base) != 0)
        {
            fprintf(stderr, "Could not create cgroup directory");
            return -1;
        }
    }

    if (open_and_write_to_subtree_control(cgroup_base) != 0)
    {
        fprintf(stderr, "Could not open and write to subtree control");
        return -1;
    }

    return 0;
}

int create_cgroup_for_container(const char *cgroup_base, pid_t current_pid, pid_t child_pid)
{
    char cgroup_dir[512];
    char control_path[640];
    char pid_text[32];

    int snprintf_result = smart_snprintf(cgroup_dir, sizeof(cgroup_dir), "%s/mini=%d", cgroup_base, current_pid);
    if (snprintf_result < 0)
    {
        perror("snprintf");
        return snprintf_result;
    }

    int mkdir_result = mkdir(cgroup_dir, 0700);
    if (mkdir_result != 0)
    {
        perror("mkdir");
        return mkdir_result;
    }

    char memory_max_file_dir[521];
    if (smart_snprintf(memory_max_file_dir, sizeof(memory_max_file_dir), "%s/memory.max", cgroup_dir) < 0)
        return -1;
    if (smart_snprintf(pid_text, sizeof(pid_text), "67108864\n") < 0)
        return -1;

    int memory_max_fd = open(memory_max_file_dir, O_WRONLY);
    if (memory_max_fd == -1)
    {
        perror("open - memory max");
        return memory_max_fd;
    }

    ssize_t memory_max_write_result = write(memory_max_fd, pid_text, strlen(pid_text));
    if (memory_max_write_result != strlen(pid_text))
    {
        perror("write - memory max");
        return memory_max_write_result;
    }
    close(memory_max_fd);

    //------
    char memory_swap_max_file_dir[521];
    if (smart_snprintf(memory_swap_max_file_dir, sizeof(memory_swap_max_file_dir), "%s/memory.swap.max", cgroup_dir) < 0)
        return -1;
    if (smart_snprintf(pid_text, sizeof(pid_text), "0\n") < 0)
        return -1;

    int memory_swap_max_fd = open(memory_swap_max_file_dir, O_WRONLY);
    if (memory_swap_max_fd == -1)
    {
        perror("open - memory swap");
        return memory_swap_max_fd;
    }

    ssize_t memory_swap_max_write_result = write(memory_swap_max_fd, pid_text, strlen(pid_text));
    if (memory_swap_max_write_result != strlen(pid_text))
    {
        perror("write - memory swap");
        return memory_swap_max_write_result;
    }
    close(memory_swap_max_fd);
    //------

    if (smart_snprintf(pid_text, sizeof(pid_text), "%d", child_pid) < 0)
        return -1;

    char procs_file_dir[525];
    if (smart_snprintf(procs_file_dir, sizeof(procs_file_dir), "%s/cgroup.procs", cgroup_dir) < 0)
        return -1;
    int procs_fd = open(procs_file_dir, O_WRONLY);
    if (procs_fd == -1)
    {
        perror("open");
        return procs_fd;
    }

    int procs_write_result = write(procs_fd, pid_text, strlen(pid_text));
    if (procs_write_result != strlen(pid_text))
    {
        perror("write");
        return procs_write_result;
    }
    close(procs_fd);

    return 0;
}

int close_write_and_wait_for_parent_approval(int *ready_pipe)
{
    close(ready_pipe[1]);

    char token;

    ssize_t read_result = read(ready_pipe[0], &token, 1);
    if (read_result <= 0)
    {
        if (read_result == 0)
        {
            fprintf(stderr, "Received EOF without permission. Terminating.");
            close(ready_pipe[0]);
            return -1;
        }
        fprintf(stderr, "Error while child reading from pipe for permission to start. Terminating.");
        close(ready_pipe[0]);
        return -1;
    }
    if (token != 'G')
    {
        fprintf(stderr, "Have not received proper \"go\" permission. Child terminating.");
        close(ready_pipe[0]);
        return -1;
    }
    fprintf(stderr, "[child] allowed to start\n");
    close(ready_pipe[0]);
    return 0;
}

int print_interfaces(void)
{
    struct if_nameindex *interfaces;

    // TODO 1: 인터페이스 배열을 받아오기.
    //         NULL이면 perror() 후 -1 반환.

    // TODO 2: 배열 끝을 나타내는 원소가 나올 때까지 반복.
    //         각 인터페이스의 번호와 이름을 출력.
    //
    //         배열 원소의 필드 접근:
    //         interfaces[i].if_index
    //         interfaces[i].if_name
    //
    //         출력 형식은 번호 %u, 이름 %s.

    // TODO 3: 배열을 전용 해제 함수에 전달해서 정리.

    return 0;
}

int main(void)
{
    pid_t current_pid = getpid();
    fprintf(stderr, "Current PID: %d\n", current_pid);

    const char *cgroup_base = "/sys/fs/cgroup/mini-container-lab";
    if (prep_cgroup(cgroup_base) != 0)
    {
        fprintf(stderr, "Failed to prepare cgroup. Terminating");
        return -1;
    }

    int unshare_result = unshare(CLONE_NEWPID);
    if (unshare_result != 0)
    {
        perror("unshare");
        fprintf(stderr, "Failed to unshare for new PID namespace. Terminating");
        return -1;
    }

    int ready_pipe[2];

    int pipe_result = pipe(ready_pipe);
    if (pipe_result != 0)
    {
        perror("pipe");
        return pipe_result;
    }

    int fflush_result = fflush(stdout);
    if (fflush_result != 0)
    {
        perror("fflush");
        fprintf(stderr, "Failed to flush stdout. Terminating");
        return fflush_result;
    }

    pid_t child_pid = fork();

    if (child_pid < 0)
    {
        perror("fork");
        fprintf(stderr, "Failed to fork, terminating");
        return -1;
    }

    if (child_pid == 0)
    {
        child_pid = getpid();

        if (close_write_and_wait_for_parent_approval(ready_pipe) != 0)
        {
            return -1;
        }

        // 부모 허가를 받은 다음.

        // TODO: "[before network unshare]" 출력.
        // TODO: print_interfaces() 호출. 실패하면 종료.

        // TODO: unshare()로 현재 자식의 network namespace 분리.
        //       사용할 플래그는 CLONE_NEWNET.
        //       실패하면 perror() 후 종료.

        // TODO: "[after network unshare]" 출력.
        // TODO: print_interfaces()를 다시 호출. 실패하면 종료.

        // 아래의 기존 mount namespace / rootfs / exec 코드는 유지.

        unshare_result = unshare(CLONE_NEWNS);
        if (unshare_result != 0)
        {
            perror("unshare_mount");
            fprintf(stderr, "unshare of mount namespace failed. Termianting.");
            return unshare_result;
        }

        int mount_flag = MS_PRIVATE | MS_REC;
        int mount_result = mount(NULL, "/", NULL, mount_flag, NULL);
        if (mount_result != 0)
        {
            perror("mount");
            fprintf(stderr, "Mounting failed. Terminating.");
            return mount_result;
        }

        const char *rootfs = "./rootfs";
        mount_flag = MS_BIND;
        mount_result = mount(rootfs, rootfs, NULL, mount_flag, NULL);
        if (mount_result != 0)
        {
            perror("mount");
            fprintf(stderr, "Mounting failed. Terminating.");
            return mount_result;
        }

        int syscall_result = syscall(SYS_pivot_root, "./rootfs", "./rootfs/oldroot");
        if (syscall_result != 0)
        {
            perror("syscall_pivot_root");
            fprintf(stderr, "Failed to system call to pivot root. Terminating.");
            return syscall_result;
        }

        int chdir_result = chdir("/");
        if (chdir_result != 0)
        {
            perror("chdir");
            fprintf(stderr, "chdir to / failed. Terminating.");
            return chdir_result;
        }

        int umount_result = umount2("/oldroot", MNT_DETACH);
        if (umount_result != 0)
        {
            perror("umount");
            fprintf(stderr, "umount failed. Terminating");
            return umount_result;
        }

        mount_result = mount("proc", "/proc", "proc", 0, NULL);
        if (mount_result != 0)
        {
            perror("mount");
            fprintf(stderr, "Mounting failed. Terminating.");
            return mount_result;
        }

        char *argv[2];
        argv[0] = "app";
        argv[1] = NULL;

        int fflush_result = fflush(stdout);
        if (fflush_result != 0)
        {
            perror("fflush");
            fprintf(stderr, "Failed to flush stdout. Terminating");
            return fflush_result;
        }

        int execv_result = execv("/bin/app", argv);

        perror("execv");
        fprintf(stderr, "Failed to replace process with execv with code: %d. Terminating.", execv_result);
        return -1;
    }
    else
    {
        fprintf(stderr, "Child's PID from Parent: %d\n", child_pid);

        close(ready_pipe[0]);

        if (create_cgroup_for_container(cgroup_base, current_pid, child_pid) != 0)
        {
            fprintf(stderr, "Failed to create cgroup for this container. Terminating");
            return -1;
        }

        fprintf(stderr, "[parent] setup complete\n");
        int fflush_result = fflush(stdout);
        if (fflush_result != 0)
        {
            perror("fflush");
            return fflush_result;
        }

        int status;
        char token = 'G';

        ssize_t write_result = write(ready_pipe[1], &token, 1);
        if (write_result != 1)
        {
            if (write_result == -1)
            {
                perror("write");

                close(ready_pipe[1]);
                pid_t waitpid_result = waitpid(child_pid, &status, 0);

                if (waitpid_result != child_pid)
                {
                    perror("child_process");
                    fprintf(stderr, "The child process has returned non-zero status code.");
                    return waitpid_result;
                }
                return write_result;
            }

            close(ready_pipe[1]);
            pid_t waitpid_result = waitpid(child_pid, &status, 0);

            if (waitpid_result != child_pid)
            {
                perror("child_process");
                fprintf(stderr, "The child process has returned non-zero status code.");

                return waitpid_result;
            }

            fprintf(stderr, "Failed to write to pipe for child due to some reason. Terminating.");
            return write_result;
        }

        close(ready_pipe[1]);

        pid_t waitpid_result = waitpid(child_pid, &status, 0);
        if (waitpid_result != child_pid)
        {
            perror("child_process");
            fprintf(stderr, "The child process has returned non-zero status code.");
            return waitpid_result;
        }

        if (WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            fprintf(stderr, "Child terminated successfully with status: %d. Terminating.\n", exit_code);
            remove_cgroup(cgroup_base);
            return 0;
        }
        else if (WIFSIGNALED(status))
        {
            int signal_number = WTERMSIG(status);
            fprintf(stderr, "Child terminated due to signal: %d. Terminating.\n", signal_number);
        }
        else
        {
            fprintf(stderr, "Child terminated abnormally.");
            return 1;
        }
    }

    return 0;
}
