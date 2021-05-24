#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "sbmem.h"

int main(int argc, char **argv) {
    int size = atoi(argv[1]);
    sbmem_init(size); 
    printf ("memory segment is created and initialized \n");
    return (0); 
}