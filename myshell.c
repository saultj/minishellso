#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>      
#include <sys/types.h>
#include <sys/wait.h>    
#include <fcntl.h>
#include "parser.h"
#include <string.h> 
#include <limits.h>
#include <signal.h> // Necesario para gestionar señales

// --- ESTRUCTURAS Y GLOBALES PARA JOBS ---
typedef struct {
    int id;             // ID del trabajo (1, 2, 3...)
    pid_t pid;          // PID del proceso
    char command[1024]; // Comando original
} job;

job jobs_list[20]; // Máximo 20 trabajos
int n_jobs = 0;    // Contador actual

// Función para borrar un trabajo de la lista (cuando termina o pasa a foreground)
void delete_job(pid_t pid) {
    int i, j;
    for (i = 0; i < n_jobs; i++) {
        if (jobs_list[i].pid == pid) {
            // Desplazar los siguientes hacia atrás
            for (j = i; j < n_jobs - 1; j++) {
                jobs_list[j] = jobs_list[j + 1];
            }
            n_jobs--;
            return;
        }
    }
}

// Función para añadir un trabajo
void add_job(pid_t pid, char *cmd) {
    if (n_jobs < 20) {
        jobs_list[n_jobs].id = n_jobs + 1; // IDs empiezan en 1
        jobs_list[n_jobs].pid = pid;
        strcpy(jobs_list[n_jobs].command, cmd);
        // Quitar el salto de línea del final si existe
        jobs_list[n_jobs].command[strcspn(jobs_list[n_jobs].command, "\n")] = 0;
        
        printf("[%d]+ Running\t\t%s\n", jobs_list[n_jobs].id, jobs_list[n_jobs].command);
        n_jobs++;
    } else {
        fprintf(stderr, "Error: Tabla de trabajos llena\n");
    }
}

int main(void) {
    char buf[1024];
    tline *line;
    pid_t pid;
    int status;
    int i;
    int p[2];
    int fd_input; 

    // --- GESTIÓN DE SEÑALES (PUNTO 1 PUNTOS) ---
    // El shell debe ignorar Ctrl+C (SIGINT) y Ctrl+\ (SIGQUIT)
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    printf("msh> "); 

    while (fgets(buf, 1024, stdin)) {
        
        // --- RECOLECTOR DE ZOMBIES ---
        // Antes de procesar nada, miramos si algún hijo en background ha muerto
        // WNOHANG hace que no se bloquee si no hay muertos
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            delete_job(pid);
        }

        line = tokenize(buf);

        if (line == NULL || line->ncommands == 0) {
            printf("msh> ");
            continue;
        }

        // --- COMANDO INTERNO: JOBS (2 PUNTOS) ---
        // Miramos argv[0] por seguridad
        if (line->commands[0].argv[0] != NULL && strcmp(line->commands[0].argv[0], "jobs") == 0) {
            for (i = 0; i < n_jobs; i++) {
                printf("[%d]+ Running\t\t%s\n", jobs_list[i].id, jobs_list[i].command);
            }
            printf("msh> ");
            continue;
        }

        // --- COMANDO INTERNO: FG (2 PUNTOS) ---
        if (line->commands[0].argv[0] != NULL && strcmp(line->commands[0].argv[0], "fg") == 0) {
            int job_index = -1;

            if (n_jobs == 0) {
                fprintf(stderr, "fg: current: no such job\n");
                printf("msh> ");
                continue;
            }

            // Si no hay argumentos, cogemos el último (LIFO)
            if (line->commands[0].argc == 1) {
                job_index = n_jobs - 1;
            } else {
                // Si hay argumento, buscamos por ID
                int target_id = atoi(line->commands[0].argv[1]);
                for (i = 0; i < n_jobs; i++) {
                    if (jobs_list[i].id == target_id) {
                        job_index = i;
                        break;
                    }
                }
            }

            if (job_index != -1) {
                pid_t target_pid = jobs_list[job_index].pid;
                printf("%s\n", jobs_list[job_index].command); // Imprimir comando
                
                // Esperamos al proceso (Foreground)
                waitpid(target_pid, &status, 0);
                
                // Lo borramos de la lista de jobs porque ya ha terminado (o ha vuelto al control)
                delete_job(target_pid);
            } else {
                fprintf(stderr, "fg: %s: no such job\n", line->commands[0].argv[1]);
            }

            printf("msh> ");
            continue;
        }

        // --- COMANDO INTERNO: CD ---
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
                if (dir == NULL) fprintf(stderr, "Error: $HOME no definido\n");
            } else {
                dir = line->commands[0].argv[1];
            }

            if (dir != NULL) {
                if (chdir(dir) < 0) {
                    perror("Error al cambiar de directorio");
                } else {
                    if (getcwd(buffer, sizeof(buffer)) != NULL) {
                        printf("%s\n", buffer);
                    }
                }
            }
            printf("msh> ");
            continue; 
        }

        // --- EJECUCIÓN NORMAL (PIPES + BACKGROUND) ---
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
                
                // IMPORTANTE: Restaurar señales en el hijo para que respondan a Ctrl+C
                signal(SIGINT, SIG_DFL);
                signal(SIGQUIT, SIG_DFL);

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

                // 3. Ejecución
                char *cmd = line->commands[i].filename;
                if (cmd == NULL) cmd = line->commands[i].argv[0];

                execvp(cmd, line->commands[i].argv);
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

        // --- GESTIÓN DE BACKGROUND (&) ---
        if (line->background) {
            // Si es background, NO esperamos. Añadimos a la lista jobs.
            // 'pid' contiene el PID del último comando lanzado en el bucle
            add_job(pid, buf);
        } else {
            // Si NO es background, esperamos a todos los hijos
            for (i = 0; i < line->ncommands; i++) {
                wait(&status);
            }
        }

        printf("msh> "); 
    }
    return 0;
}