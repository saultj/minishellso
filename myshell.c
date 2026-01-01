#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>      // Para fork(), execvp()
#include <sys/types.h>   // Para pid_t
#include <sys/wait.h>    // Para wait()
#include "parser.h"      // Librería del profesor

int main(void) {
    char buf[1024];
    tline *line;
    pid_t pid;
    int status;

    printf("msh> "); // El prompt

    // Bucle infinito: lee línea a línea del teclado
    while (fgets(buf, 1024, stdin)) {
        
        // El parser trocea la línea por nosotros
        line = tokenize(buf);

        // Si la línea está vacía, pedimos otra vez
        if (line == NULL || line->ncommands == 0) {
            printf("msh> ");
            continue;
        }

        // --- EJECUCIÓN DEL COMANDO ---
        pid = fork(); // 1. Creamos proceso hijo

        if (pid < 0) { 
            fprintf(stderr, "Error al hacer fork\n");
            exit(1);
        }
        else if (pid == 0) { 
            // 2. CÓDIGO DEL HIJO
            // Ejecuta el comando (ej: ls, whoami)
            execvp(line->commands[0].filename, line->commands[0].argv);
            
            // Si llega aquí, es que falló (comando no existe)
            fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[0].filename);
            exit(1);
        }
        else {
            // 3. CÓDIGO DEL PADRE
            wait(&status); // Espera a que el hijo termine
        }

        printf("msh> "); // Vuelve a sacar el prompt
    }
    return 0;
}