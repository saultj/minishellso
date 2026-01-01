#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>      // Para fork(), execvp()
#include <sys/types.h>   // Para pid_t
#include <sys/wait.h>    // Para wait()
#include <fcntl.h>       // ¡IMPORTANTE! Para open(), O_CREAT, O_RDONLY
#include "parser.h"      // Librería del profesor

int main(void) {
    char buf[1024];
    tline *line;
    pid_t pid;
    int status;

    printf("msh> "); 

    while (fgets(buf, 1024, stdin)) {
        
        line = tokenize(buf);

        if (line == NULL || line->ncommands == 0) {
            printf("msh> ");
            continue;
        }

        // --- EJECUCIÓN ---
        pid = fork(); 

        if (pid < 0) { 
            fprintf(stderr, "Error al hacer fork\n");
            exit(1);
        }
        else if (pid == 0) { 
            // --- CÓDIGO DEL HIJO ---

            // 1. Redirección de Entrada (<)
            if (line->redirect_input != NULL) {
                int fd = open(line->redirect_input, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "%s: Error. No se puede abrir entrada\n", line->redirect_input);
                    exit(1);
                }
                dup2(fd, 0); 
                close(fd);
            }

            // 2. Redirección de Salida (>)
            if (line->redirect_output != NULL) {
                int fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd < 0) {
                    fprintf(stderr, "%s: Error. No se puede abrir salida\n", line->redirect_output);
                    exit(1);
                }
                dup2(fd, 1); 
                close(fd);
            }

            // 3. Ejecutar el mandato
            execvp(line->commands[0].filename, line->commands[0].argv);
            
            // Si llega aquí, es que falló
            fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[0].filename);
            exit(1);
        }
        else {
            // --- CÓDIGO DEL PADRE ---
            wait(&status); 
        }

        printf("msh> "); 
    }
    return 0;
}