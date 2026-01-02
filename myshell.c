#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>      
#include <sys/types.h>
#include <sys/wait.h>    
#include <fcntl.h>
#include "parser.h"
#include <string.h> 
#include <limits.h>    

int main(void) {
    char buf[1024];
    tline *line;
    pid_t pid;
    int status;
    int i;
    int p[2];
    int fd_input; 

    printf("msh> "); 

    while (fgets(buf, 1024, stdin)) {
        
        line = tokenize(buf);

        if (line == NULL || line->ncommands == 0) {
            printf("msh> ");
            continue;
        }

        // --- SOLUCIÓN DEL CD ---
        // Miramos argv[0] en lugar de filename, porque para 'cd', filename es NULL
        int is_cd = 0;
        if (line->commands[0].argv[0] != NULL) {
            if (strcmp(line->commands[0].argv[0], "cd") == 0) {
                is_cd = 1;
            }
        }

        if (is_cd) {
            char *dir = NULL;
            char buffer[4096]; 

            if (line->commands[0].argc == 1) {
                dir = getenv("HOME");
                if (dir == NULL) {
                    fprintf(stderr, "Error: $HOME no definido\n");
                }
            } else {
                dir = line->commands[0].argv[1];
            }

            // Si dir es NULL (ej: cd sin HOME), no intentamos chdir
            if (dir != NULL) {
                if (chdir(dir) < 0) {
                    perror("Error al cambiar de directorio");
                } else {
                    // Imprimir ruta absoluta (Requisito PDF)
                    if (getcwd(buffer, sizeof(buffer)) != NULL) {
                        printf("%s\n", buffer);
                    } else {
                        perror("Error getcwd");
                    }
                }
            }
            
            printf("msh> ");
            continue; // Saltamos los pipes
        }

        // --- EJECUCIÓN NORMAL (PIPES) ---
        fd_input = 0; 
        for (i = 0; i < line->ncommands; i++) {
            
            if (i < line->ncommands - 1) {
                pipe(p);
            }
            
            pid = fork();

            if (pid < 0) {
                perror("Fork falló");
                exit(1);
            }

            else if (pid == 0) { // HIJO
                
                // 1. Redirección Entrada
                if (i > 0) {
                    dup2(fd_input, 0);
                    close(fd_input);
                } else {
                    if (line->redirect_input != NULL) {
                        int fd = open(line->redirect_input, O_RDONLY);
                        if (fd < 0) { perror("Error entrada"); exit(1); }
                        dup2(fd, 0); close(fd);
                    }
                }

                // 2. Redirección Salida
                if (i < line->ncommands - 1) {
                    dup2(p[1], 1);
                    close(p[0]); close(p[1]);
                } else {
                    if (line->redirect_output != NULL) {
                        int fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (fd < 0) { perror("Error salida"); exit(1); }
                        dup2(fd, 1); close(fd);
                    }
                }

                // 3. Ejecución (Blindada contra NULL)
                // Si filename es NULL (comando desconocido), usamos argv[0] para que execvp intente buscarlo y reporte error estándar
                char *cmd = line->commands[i].filename;
                if (cmd == NULL) {
                    cmd = line->commands[i].argv[0];
                }

                execvp(cmd, line->commands[i].argv);
                
                // Si llegamos aquí, execvp falló
                fprintf(stderr, "%s: No se encuentra el mandato\n", cmd);
                exit(1);
            }

            else { // PADRE
                if (i > 0) close(fd_input);
                if (i < line->ncommands - 1) {
                    fd_input = p[0];
                    close(p[1]);
                }
            }
        } 

        for (i = 0; i < line->ncommands; i++) {
            wait(&status);
        }

        printf("msh> "); 
    }
    return 0;
}