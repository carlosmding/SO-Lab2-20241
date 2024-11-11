#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h> // Necesario para redirección y uso de open()

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_PATH 10

char error_message[30] = "An error has occurred\n";

char *path[MAX_PATH];
int path_count = 0;

void initialize_path() {
    path[0] = strdup("/bin/");
    path_count = 1; // Solo hay un directorio en el path inicial
}

void free_path() {
    for (int i = 0; i < path_count; i++) {
        free(path[i]);
        path[i] = NULL;
    }
    path_count = 0;
}

void run_exit(char **args, int num_args) {
    if (num_args > 1) { // Si hay más de un argumento para `exit`, es un error
        write(STDERR_FILENO, error_message, strlen(error_message));
    } else {
        exit(0);
    }
}

void run_cd(char **args, int num_args) {
    if (num_args != 2) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        return;
    }
    if (chdir(args[1]) != 0) {
        write(STDERR_FILENO, error_message, strlen(error_message));
    }
}

void set_path(char **args, int num_args) {
    // Limpiar cualquier path previamente establecido
    free_path();

    // Verificar que se haya pasado al menos un directorio al comando `path`
    if (num_args < 2) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        return;
    }

    // Establecer el nuevo path
    for (int i = 1; i < num_args; i++) {
        path[path_count++] = strdup(args[i]);
    }
}


int run_external_command(char **args) {
    int redirect_count = 0;
    int last_redirect_index = -1;
    int i;

    // Contar todas las redirecciones y obtener la última
    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            redirect_count++;
            last_redirect_index = i;
        }
    }

    // Error si hay más de una redirección
    if (redirect_count > 1) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        return -1;
    }

    // Si hay exactamente una redirección
    if (redirect_count == 1) {
        // Verificar que hay un archivo después del último ">"
        if (args[last_redirect_index + 1] == NULL || args[last_redirect_index + 2] != NULL) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return -1;
        }

        // Abrir el archivo de salida
        int output_fd = open(args[last_redirect_index + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output_fd == -1) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return -1;
        }

        // Redirigir stdout al archivo
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            close(output_fd);
            return -1;
        }

        close(output_fd); // Cerrar el descriptor después de redirigir

        // Terminar la lista de argumentos en el símbolo de redirección
        args[last_redirect_index] = NULL;
    }

    // Intentar ejecutar el comando directamente en el directorio actual
    char command_path[256];
    snprintf(command_path, sizeof(command_path), "%s", args[0]);
    if (access(command_path, X_OK) == 0) {
        if (execv(command_path, args) == -1) { // Si execv falla, retorna inmediatamente
            write(STDERR_FILENO, error_message, strlen(error_message));
            return -1;
        }
    }

    // Si no se encuentra el comando, buscar en los directorios del `path`
    for (i = 0; i < path_count; i++) {
        snprintf(command_path, sizeof(command_path), "%s/%s", path[i], args[0]);
        if (access(command_path, X_OK) == 0) {
            if (execv(command_path, args) == -1) { // Si execv falla, retorna inmediatamente
                write(STDERR_FILENO, error_message, strlen(error_message));
                return -1;
            }
        }
    }

    // Si no se encontró el comando en ningún directorio del `path`
    write(STDERR_FILENO, error_message, strlen(error_message));
    return -1;
}


static int parse_input(char *input, char **args) {
    int num_args = 0;
    char *token;

    while ((token = strsep(&input, " \t\n")) != NULL) {
        if (*token != '\0') { // Ignorar cadenas vacías
            args[num_args++] = token;
            if (num_args >= MAX_ARGS) break; // Limitar el número de argumentos
        }
    }
    args[num_args] = NULL; // Terminar con NULL para execv
    return num_args;
}

int parse_commands(char *input, char **commands) {
    int num_commands = 0;
    char *command;

    while ((command = strsep(&input, "&")) != NULL) {
        if (*command != '\0') { // Ignorar cadenas vacías
            commands[num_commands++] = command;
            if (num_commands >= MAX_ARGS) break; // Limitar el número de comandos
        }
    }
    commands[num_commands] = NULL;
    return num_commands;
}

void execute_commands(char **commands) {
    pid_t pids[MAX_ARGS];
    int i = 0;

    while (commands[i] != NULL) {
        char *args[MAX_ARGS];
        int num_args = parse_input(commands[i], args);

        if (num_args == 0) {
            i++;
            continue; // Ignorar comandos vacíos
        }

        if (strcmp(args[0], "exit") == 0) {
            run_exit(args, num_args);
        } else if (strcmp(args[0], "cd") == 0) {
            run_cd(args, num_args);
        } else if (strcmp(args[0], "path") == 0) {
            set_path(args, num_args);
        } else {
            pid_t pid = fork();
            if (pid == 0) {
                run_external_command(args);
                exit(1);
            } else if (pid > 0) {
                pids[i] = pid;
            } else {
                write(STDERR_FILENO, error_message, strlen(error_message));
            }
        }
        i++;
    }

    for (int j = 0; j < i; j++) {
        if (pids[j] > 0) {
            waitpid(pids[j], NULL, 0);
        }
    }
}

void shell_loop(FILE *input_stream) {
    char input[MAX_INPUT];
    char *commands[MAX_ARGS];

    while (1) {
        if (input_stream == stdin) {
            printf("wish> ");
            fflush(stdout);
        }

        if (fgets(input, sizeof(input), input_stream) == NULL) {
            break;
        }

        input[strcspn(input, "\n")] = 0;

        int num_commands = parse_commands(input, commands);
        if (num_commands == 0) continue;

        execute_commands(commands);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 2) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        return 1;
    }

    initialize_path();

    if (argc == 1) {
        shell_loop(stdin);
    } else if (argc == 2) {
        FILE *batch_file = fopen(argv[1], "r");
        if (batch_file == NULL) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return 1;
        }
        shell_loop(batch_file);
        fclose(batch_file);
    }

    free_path();
    return 0;
}
