#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

// Tamaño máximo de entrada y argumentos
#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_PATH 10

// Mensaje de error
char error_message[30] = "Ha ocurrido un error\n";

// Variable path y su tamaño actual
char *path[MAX_PATH];
int path_count = 0;

// Inicialización de path con valores por defecto
void initialize_path() {
    path[0] = strdup("./");
    path[1] = strdup("/usr/bin/");
    path[2] = strdup("/bin/");
    path_count = 3;
}

// Liberar memoria de path
void free_path() {
    for (int i = 0; i < path_count; i++) {
        free(path[i]);
        path[i] = NULL;
    }
    path_count = 0;
}

// Comando built-in `exit`
void run_exit() {
    exit(0);
}

// Comando built-in `cd`
void run_cd(char **args, int num_args) {
    if (num_args != 2) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        return;
    }
    if (chdir(args[1]) != 0) {
        write(STDERR_FILENO, error_message, strlen(error_message));
    }
}

// Comando built-in `path`
void set_path(char **args, int num_args) {
    free_path(); // Limpiar el path actual
    for (int i = 1; i < num_args; i++) {
        if (path_count < MAX_PATH) {
            path[path_count] = strdup(args[i]);
            path_count++;
        } else {
            break;
        }
    }
}

// Ejecutar comandos externos
int run_external_command(char **args) {
    char command_path[256];
    for (int i = 0; i < path_count; i++) {
        snprintf(command_path, sizeof(command_path), "%s%s", path[i], args[0]);
        if (access(command_path, X_OK) == 0) {
            execv(command_path, args);
            perror("execv");
            return -1;
        }
    }
    write(STDERR_FILENO, error_message, strlen(error_message));
    return -1;
}

// Analizar entrada en tokens
int parse_input(char *input, char **args) {
    int num_args = 0;
    char *token = strtok(input, " \t\n");
    while (token != NULL && num_args < MAX_ARGS) {
        args[num_args++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[num_args] = NULL; // Terminar con NULL para execv
    return num_args;
}

// Loop principal del shell
void shell_loop() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    int num_args;

    while (1) {
        printf("wish> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break; // EOF
        }
        
        num_args = parse_input(input, args);
        if (num_args == 0) continue; // Entrada vacía

        // Procesar comandos built-in
        if (strcmp(args[0], "exit") == 0) {
            run_exit();
        } else if (strcmp(args[0], "cd") == 0) {
            run_cd(args, num_args);
        } else if (strcmp(args[0], "path") == 0) {
            set_path(args, num_args);
        } else {
            // Ejecutar comando externo
            pid_t pid = fork();
            if (pid == 0) {
                // Proceso hijo
                run_external_command(args);
                exit(1); // En caso de fallo
            } else if (pid > 0) {
                // Proceso padre espera al hijo
                wait(NULL);
            } else {
                write(STDERR_FILENO, error_message, strlen(error_message));
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc > 2) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        exit(1);
    }

    initialize_path(); // Inicializar path por defecto
    shell_loop();      // Ejecutar loop del shell
    free_path();       // Liberar path al final

    return 0;
}
