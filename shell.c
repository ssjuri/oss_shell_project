#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

// 상수 정의
#define BUF_SIZE 256
#define MAX_ARGS 50

// 전역 변수
char buf[BUF_SIZE];
char *argv[MAX_ARGS];
int narg, status;
pid_t pid;
int IS_BACKGROUND = 0;

// 함수 선언
void initialize_shell();
int parse_arguments(char *cmd, char *argv[]);
void execute_shell();
void handle_input_redirection(char **argv);
void handle_output_redirection(char **argv);
void handle_append_redirection(char **argv);
int handle_pipes_and_execute(char **argv);
void execute_with_redirection(char **argv);
void change_directory_command(int argc, char *argv[]);
void setup_signal_handling();
void handle_sigint(int signo);
void handle_sigtstp(int signo);
void handle_sigchld(int signo);

// 시그널 핸들러
void handle_sigint(int signo) {
    if (pid > 0) {
        printf("\nCTRL+C (SIGINT) 수신. 현재 프로세스를 중지합니다...\n");
        kill(pid, SIGINT);
    } else {
        printf("\nCTRL+C (SIGINT) 수신.\n");
    }
}

void handle_sigtstp(int signo) {
    if (pid > 0) {
        printf("\nCTRL+Z (SIGTSTP) 수신. 현재 프로세스를 일시 중지합니다...\n");
        kill(pid, SIGTSTP);
    } else {
        printf("\nCTRL+Z (SIGTSTP) 수신.\n");
    }
}

void handle_sigchld(int signo) {
    pid_t finished_pid;
    int status;

    // 수집하지 않은 모든 자식 프로세스 수집
    while ((finished_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[프로세스 %d 완료]\n", finished_pid);
    }
}

// 쉘 초기화
void initialize_shell() {
    setup_signal_handling();
    printf("\n쉘에 오신 것을 환영합니다!\n");
}

// 시그널 처리 설정
void setup_signal_handling() {
    struct sigaction sigint_action, sigquit_action, sigchld_action;

    // SIGINT (CTRL+C)
    sigint_action.sa_handler = handle_sigint;
    sigfillset(&(sigint_action.sa_mask));
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);

    // SIGTSTP (CTRL+Z)
    sigquit_action.sa_handler = handle_sigtstp;
    sigfillset(&(sigquit_action.sa_mask));
    sigquit_action.sa_flags = 0;
    sigaction(SIGTSTP, &sigquit_action, NULL);

    // SIGCHLD (백그라운드 프로세스 종료 감지)
    sigchld_action.sa_handler = handle_sigchld;
    sigfillset(&(sigchld_action.sa_mask));
    sigchld_action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sigchld_action, NULL);
}

// 명령어 인자 파싱
int parse_arguments(char *cmd, char *argv[]) {
    int narg = 0;
    while (*cmd) {
        while (*cmd == ' ' || *cmd == '\t') *cmd++ = '\0';
        if (*cmd == '\0') break;
        if (*cmd == '&') {
            IS_BACKGROUND = 1;
            *cmd++ = '\0';
        } else {
            argv[narg++] = cmd++;
        }
        while (*cmd && *cmd != ' ' && *cmd != '\t') cmd++;
    }
    argv[narg] = NULL;
    return narg;
}

// 디렉터리 변경 구현
void change_directory_command(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "cd: 인자가 없습니다\n");
    } else if (chdir(argv[1]) == -1) {
        perror("cd");
    }
}

// 파일 리디렉션 함수
void handle_input_redirection(char **argv) {
    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "<") == 0) {
            int fd = open(argv[i + 1], O_RDONLY);
            if (fd == -1) {
                perror(argv[i + 1]);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
            argv[i] = NULL;
            break;
        }
    }
}

void handle_output_redirection(char **argv) {
    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], ">") == 0) {
            int fd = open(argv[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                perror(argv[i + 1]);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            argv[i] = NULL;
            break;
        }
    }
}

void handle_append_redirection(char **argv) {
    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], ">>") == 0) {
            int fd = open(argv[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd == -1) {
                perror(argv[i + 1]);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            argv[i] = NULL;
            break;
        }
    }
}

// 리디렉션 및 명령 실행
void execute_with_redirection(char **argv) {
    handle_input_redirection(argv);
    handle_output_redirection(argv);
    handle_append_redirection(argv);

    execvp(argv[0], argv);
    perror("execvp");
    exit(EXIT_FAILURE);
}

// 파이프 구현
int handle_pipes_and_execute(char **argv) {
    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "|") == 0) {
            argv[i] = NULL;

            int fd[2];
            if (pipe(fd) == -1) {
                perror("pipe");
                return -1;
            }

            pid_t pid1 = fork();
            if (pid1 == 0) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
                execute_with_redirection(argv);
            }

            pid_t pid2 = fork();
            if (pid2 == 0) {
                dup2(fd[0], STDIN_FILENO);
                close(fd[1]);
                close(fd[0]);
                execute_with_redirection(&argv[i + 1]);
            }

            close(fd[0]);
            close(fd[1]);
            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);

            return 1;
        }
    }
    return 0;
}

// 쉘 실행
void execute_shell() {
    while (1) {
        IS_BACKGROUND = 0;
        printf("shell> ");
        fgets(buf, BUF_SIZE, stdin);
        buf[strcspn(buf, "\n")] = '\0';

        if (strcmp(buf, "exit") == 0) {
            printf("쉘을 종료합니다.\n");
            exit(0);
        }

        narg = parse_arguments(buf, argv);
        if (narg == 0) continue;

        if (strcmp(argv[0], "cd") == 0) {
            change_directory_command(narg, argv);
            continue;
        }

        pid = fork();
        if (pid == 0) {
            setup_signal_handling();
            if (!handle_pipes_and_execute(argv)) {
                execute_with_redirection(argv);
            }
            exit(EXIT_SUCCESS);
        } else if (pid > 0) {
            if (IS_BACKGROUND) {
                printf("[프로세스 %d가 백그라운드에서 실행 중]\n", pid);
            } else {
                waitpid(pid, &status, 0);
            }
        } else {
            perror("fork");
        }
    }
}

// 메인 함수
int main() {
    initialize_shell();
    execute_shell();
    return 0;
}
