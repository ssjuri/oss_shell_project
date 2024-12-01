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
int parse_command_arguments(char *cmd, char *argv[]);
void execute_shell();
void handle_input_redirection(char **argv);
void handle_output_redirection(char **argv);
void handle_append_redirection(char **argv);
int process_pipeline_and_execute(char **argv);
void handle_redirection_and_execute(char **argv);
void execute_change_directory(int argc, char *argv[]);
void signal_handler_sigint();
void signal_handler_sigtstp();
void setup_signal_handlers();

// 신호 핸들러
void handler_sigint(int signo) {
    if (pid > 0) {
        printf("\nCTRL+C (SIGINT) 신호를 받았습니다. 현재 프로세스를 중지합니다...\n");
        kill(pid, SIGINT);
    } else {
        printf("\nCTRL+C (SIGINT) 신호를 받았습니다.\n");
    }
}

void handler_sigtstp(int signo) {
    if (pid > 0) {
        printf("\nCTRL+Z (SIGTSTP) 신호를 받았습니다. 현재 프로세스를 일시 중지합니다...\n");
        kill(pid, SIGTSTP);
    } else {
        printf("\nCTRL+Z (SIGTSTP) 신호를 받았습니다.\n");
    }
}

// 셸 초기화
void initialize_shell() {
    setup_signal_handlers();
    printf("\n쉘에 오신 것을 환영합니다!\n");
}

// 신호 처리 설정
void setup_signal_handlers() {
    struct sigaction sigint_action, sigtstp_action;

    sigint_action.sa_handler = handler_sigint;
    sigfillset(&(sigint_action.sa_mask));
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);

    sigtstp_action.sa_handler = handler_sigtstp;
    sigfillset(&(sigtstp_action.sa_mask));
    sigtstp_action.sa_flags = 0;
    sigaction(SIGTSTP, &sigtstp_action, NULL);
}

// 명령어 인수 분석
int parse_command_arguments(char *cmd, char *argv[]) {
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

// 디렉토리 변경 기능
void execute_change_directory(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "cd: 인수가 누락되었습니다\n");
    } else if (chdir(argv[1]) == -1) {
        perror("cd");
    }
}

// 파일 리다이렉션 처리 함수
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

// 리다이렉션과 명령 실행
void handle_redirection_and_execute(char **argv) {
    handle_input_redirection(argv);
    handle_output_redirection(argv);
    handle_append_redirection(argv);

    execvp(argv[0], argv);
    perror("execvp");
    exit(EXIT_FAILURE);
}

// 파이프라인 처리 및 실행
int process_pipeline_and_execute(char **argv) {
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
                handle_redirection_and_execute(argv);
            }

            pid_t pid2 = fork();
            if (pid2 == 0) {
                dup2(fd[0], STDIN_FILENO);
                close(fd[1]);
                close(fd[0]);
                handle_redirection_and_execute(&argv[i + 1]);
            }

            close(fd[0]);
            close(fd[1]);
            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);

            return 1;
        }
    }
    return 0; // 파이프가 없는 경우
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

        narg = parse_command_arguments(buf, argv);
        if (narg == 0) continue;

        if (strcmp(argv[0], "cd") == 0) {
            execute_change_directory(narg, argv);
            continue;
        }

        pid = fork();
        if (pid == 0) {
            setup_signal_handlers();
            if (!process_pipeline_and_execute(argv)) {
                handle_redirection_and_execute(argv);
            }
            exit(EXIT_SUCCESS);
        } else if (pid > 0) {
            if (IS_BACKGROUND) {
                printf("[프로세스 %d 백그라운드에서 실행 중]\n", pid);
            } else {
                waitpid(pid, &status, 0);
            }
        } else {
            perror("fork");
        }

        // 완료된 백그라운드 프로세스 확인
        int wpid;
        while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("[%d] 완료\n", wpid);
        }
    }
}

// 메인 함수
int main() {
    initialize_shell();
    execute_shell();
    return 0;
}
