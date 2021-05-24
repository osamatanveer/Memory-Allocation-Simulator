#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>

#include "sbmem.h"

// Define a name for your shared memory; you can give any name that start with a slash character; it will be like a filename.
#define MEMORY_NAME "/shared_memory"
#define MAX_NUMBER_OF_PROCESSES 10
#define SEM_NAME "semaphore"
// Define semaphore(s)
sem_t *sem;

// Define your stuctures and variables.
int fd = -10;
extern int errno;

struct TableInfo {
    int bookKeepingSize;
    int allocatableSize;
    pid_t pids[MAX_NUMBER_OF_PROCESSES];
    int interestedProcessesCount;
};

struct allocation {
    int start;
    int end;
};

struct possible_allocations {
    struct allocation possibilities[2048];
    int length;
};

struct all_nodes {
    struct possible_allocations allFree[19];
    int length;
};

struct allocations {
    struct allocation allocs[2048];
    int length;
};

void *ptr;

int sbmem_init(int segmentsize) {
    if (segmentsize == 0) return -1;
    if ((segmentsize & (segmentsize - 1)) != 0) return -1;
    if ((segmentsize / 1024) < 32 || (segmentsize / 1024) > 256) return -1;

    sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 1); 
    if (sem == SEM_FAILED) {
        sem_unlink(SEM_NAME);
        sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 1); 
    }
    if (sem == SEM_FAILED) return -1;

    shm_unlink(MEMORY_NAME);

    int bookKeepingMemorySize = 0;
    bookKeepingMemorySize += sizeof(struct TableInfo);
    bookKeepingMemorySize += sizeof(struct all_nodes);
    bookKeepingMemorySize += sizeof(struct allocations);
    
    int pos = ceil(log2(bookKeepingMemorySize));
    bookKeepingMemorySize = pow(2, pos);

    fd = shm_open(MEMORY_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666);
    int totalSize = segmentsize + bookKeepingMemorySize;
    if (fd < 0 || ftruncate(fd, totalSize) < 0) return -1;
    void *ptr = mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) return -1;

    struct TableInfo* tableInfoPtr = (struct TableInfo*) ptr;
    tableInfoPtr->bookKeepingSize = bookKeepingMemorySize;
    tableInfoPtr->allocatableSize = segmentsize;

    int highestPossiblePowerOf2 = ceil(log2(segmentsize));
    void *iterPtr = ptr;
    iterPtr += sizeof(struct TableInfo);

    struct all_nodes* allNodesPtr = (struct all_nodes*) iterPtr;
    allNodesPtr->allFree[highestPossiblePowerOf2].possibilities[0].start = 0;
    allNodesPtr->allFree[highestPossiblePowerOf2].possibilities[0].end = segmentsize - 1;
    allNodesPtr->allFree[highestPossiblePowerOf2].length = 1;
    allNodesPtr->length = highestPossiblePowerOf2 + 1;
    munmap(NULL, totalSize);

    return 0;    
}

int sbmem_remove() {
    sem_unlink(SEM_NAME);
    sem_destroy(sem);
    if (shm_unlink(MEMORY_NAME) != 0) return -1;
    return 0;
} 

int sbmem_open() {
    fd = shm_open(MEMORY_NAME, O_RDWR, 0666);
    sem = sem_open(SEM_NAME, O_RDWR); 
    if (sem == SEM_FAILED) return -1;
    sem_wait(sem);
    ptr = mmap(NULL, sizeof(struct TableInfo), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    struct TableInfo *tableInfoPtr = (struct TableInfo*) ptr; 
    int totalSize = tableInfoPtr->bookKeepingSize + tableInfoPtr->allocatableSize;
    munmap(NULL, sizeof(struct TableInfo));
   
    ptr = mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int interestedProcessesCount = tableInfoPtr->interestedProcessesCount;
    int *pids = tableInfoPtr->pids;

    int i;
    for (i = 0; i < interestedProcessesCount; i++) {
        if (pids[i] == getpid()) { 
            sem_post(sem);
            return 0;
        }
    }
    if (i == 10) {
        sem_post(sem);
        return -1;
    }

    pids[i] = getpid();
    tableInfoPtr->interestedProcessesCount++;
    
    close(fd);
    sem_post(sem);
    return 0;
} 

void *sbmem_alloc (int size) {
    if (size < 128 || size > 4096) return NULL;
    
    sem_wait(sem);
    struct TableInfo* tableInfoPtr = (struct TableInfo*) ptr;

    void *allocatableMemoryStart = ptr;
    allocatableMemoryStart += tableInfoPtr->bookKeepingSize;

    void *iterPtr = ptr;
    iterPtr += sizeof(struct TableInfo);

    struct all_nodes* allNodesPtr = (struct all_nodes*) iterPtr;
    iterPtr += sizeof(struct all_nodes);

    struct allocations* allocationsPtr = (struct allocations*) iterPtr;


    int possiblePosition = ceil(log2(size)); 
    int i;
    struct allocation temp;

    if (allNodesPtr->allFree[possiblePosition].length > 0) {
        temp.start = allNodesPtr->allFree[possiblePosition].possibilities[0].start;
        temp.end = allNodesPtr->allFree[possiblePosition].possibilities[0].end;
        allNodesPtr->allFree[possiblePosition].possibilities[0].start = 0;
        allNodesPtr->allFree[possiblePosition].possibilities[0].end = 0;
        if (allNodesPtr->allFree[possiblePosition].possibilities[1].start != 0 || allNodesPtr->allFree[possiblePosition].possibilities[1].end != 0) {
            for (int j = 0; j < allNodesPtr[j].length - 1; j++) {
                allNodesPtr->allFree[possiblePosition].possibilities[j].start = allNodesPtr->allFree[possiblePosition].possibilities[j + 1].start;
                allNodesPtr->allFree[possiblePosition].possibilities[j].end = allNodesPtr->allFree[possiblePosition].possibilities[j + 1].end;
            }
        }
        allNodesPtr->allFree[possiblePosition].length--;
        allocationsPtr->allocs[allocationsPtr->length].start = temp.start;
        allocationsPtr->allocs[allocationsPtr->length].end = temp.end;
        allocationsPtr->length++;
        sem_post(sem);
        return allocatableMemoryStart + temp.start;
    }
    for (i = possiblePosition + 1; i < allNodesPtr->length; i++) {  
        if (allNodesPtr->allFree[i].length == 0) continue;
        break; 
    }

    if (i >= allNodesPtr->length) {
        sem_post(sem);
        return NULL;
    }

    temp.start = allNodesPtr->allFree[i].possibilities[0].start;
    temp.end = allNodesPtr->allFree[i].possibilities[0].end;
    allNodesPtr->allFree[i].possibilities[0].start = 0;
    allNodesPtr->allFree[i].possibilities[0].end = 0;
    if (allNodesPtr->allFree[i].possibilities[1].start != 0 || allNodesPtr->allFree[i].possibilities[1].end != 0) {
        for (int j = 0; j < allNodesPtr[i].length - 1; j++) {
            allNodesPtr->allFree[i].possibilities[i].start = allNodesPtr->allFree[i + 1].possibilities[i].start;
            allNodesPtr->allFree[i].possibilities[i].end = allNodesPtr->allFree[i + 1].possibilities[i].end;
        }
    }
    allNodesPtr->allFree[i].length--;
    i--;

    for (; i >= possiblePosition; i--) {
        struct allocation newAlloc;
        newAlloc.start = temp.start;
        newAlloc.end = temp.start + (temp.end - temp.start) / 2;
        
        
        struct allocation newAlloc2;
        newAlloc2.start = temp.start + (temp.end - temp.start + 1) / 2;
        newAlloc2.end = temp.end;

        allNodesPtr->allFree[i].possibilities[allNodesPtr->allFree[i].length].start = newAlloc.start;
        allNodesPtr->allFree[i].possibilities[allNodesPtr->allFree[i].length].end = newAlloc.end;
        allNodesPtr->allFree[i].length++;

        allNodesPtr->allFree[i].possibilities[allNodesPtr->allFree[i].length].start = newAlloc2.start;
        allNodesPtr->allFree[i].possibilities[allNodesPtr->allFree[i].length].end = newAlloc2.end;
        allNodesPtr->allFree[i].length++;
        
        temp.start = allNodesPtr->allFree[i].possibilities[0].start;
        temp.end = allNodesPtr->allFree[i].possibilities[0].end;
        allNodesPtr->allFree[i].possibilities[0].start = 0; 
        allNodesPtr->allFree[i].possibilities[0].end = 0; 
        if (allNodesPtr->allFree[i].possibilities[1].start != 0 || allNodesPtr->allFree[i].possibilities[1].end != 0) {
            for (int j = 1; j < allNodesPtr->allFree[i].length; j++) {
                allNodesPtr->allFree[i].possibilities[j - 1].start = allNodesPtr->allFree[i].possibilities[j].start;
                allNodesPtr->allFree[i].possibilities[j - 1].end = allNodesPtr->allFree[i].possibilities[j].end;
                allNodesPtr->allFree[i].possibilities[j].start = 0;
                allNodesPtr->allFree[i].possibilities[j].end = 0;
            }
        }
        allNodesPtr->allFree[i].length--;
    }
    allocationsPtr->allocs[allocationsPtr->length].start = temp.start;
    allocationsPtr->allocs[allocationsPtr->length].end = temp.end;
    allocationsPtr->length++;
    
    void* ptrToReturn = allocatableMemoryStart + temp.start;
    sem_post(sem);
    return ptrToReturn;
}

void sbmem_free(void *p) {
    sem_wait(sem);
    void *iterPtr = ptr;

    struct TableInfo* tableInfoPtr = (struct TableInfo*) iterPtr;
    iterPtr += sizeof(struct TableInfo);

    struct all_nodes* allNodesPtr = (struct all_nodes*) iterPtr;
    iterPtr += sizeof(struct all_nodes);

    struct allocations* allocationsPtr = (struct allocations*) iterPtr;
    
    int startingPosition = p - (ptr + tableInfoPtr->bookKeepingSize);
    struct allocation allocatedBlock;
    for (int j = 0; j < allocationsPtr->length; j++) {
        if (allocationsPtr->allocs[j].start == startingPosition) {
            allocatedBlock.start = allocationsPtr->allocs[j].start;
            allocatedBlock.end = allocationsPtr->allocs[j].end;
            break;
        }
    } 
    int allocationSize = allocatedBlock.end - allocatedBlock.start + 1;
    int possiblePosition = ceil(log2(allocationSize));

    int i;
    allNodesPtr->allFree[possiblePosition].possibilities[allNodesPtr->allFree[possiblePosition].length].start = allocatedBlock.start;
    allNodesPtr->allFree[possiblePosition].possibilities[allNodesPtr->allFree[possiblePosition].length].end = allocatedBlock.start + ((int) pow(2, possiblePosition)) - 1;
    allNodesPtr->allFree[possiblePosition].length++;
    
    int address;
    if ((allocatedBlock.start / allocationSize) % 2 != 0) {
        address = allocatedBlock.start - ((int) pow(2, possiblePosition));
    } else {
        address = allocatedBlock.start + ((int) pow(2, possiblePosition));
    }
    
    for (i = 0; i < allNodesPtr->allFree[possiblePosition].length; i++) {  
        if (allNodesPtr->allFree[possiblePosition].possibilities[i].end  == address) {
            if ((allocatedBlock.start / allocationSize) % 2 == 0) {
                allNodesPtr->allFree[possiblePosition + 1].possibilities[allNodesPtr->allFree[possiblePosition+1].length].start = allocatedBlock.start;
                allNodesPtr->allFree[possiblePosition + 1].possibilities[allNodesPtr->allFree[possiblePosition+1].length].start = allocatedBlock.start + 2 * ((int)pow(2, possiblePosition)) - 1;
            } else {      
                allNodesPtr->allFree[possiblePosition + 1].possibilities[allNodesPtr->allFree[possiblePosition+1].length].start = address;
                allNodesPtr->allFree[possiblePosition + 1].possibilities[allNodesPtr->allFree[possiblePosition+1].length].end = address + 2 * ((int) pow(2, possiblePosition)) - 1;
            }
            for (int j = i; j < allNodesPtr->allFree[possiblePosition].length - 1; j++) {
                allNodesPtr->allFree[possiblePosition].possibilities[j].start = allNodesPtr->allFree[possiblePosition].possibilities[j + 1].start;
                allNodesPtr->allFree[possiblePosition].possibilities[j].end = allNodesPtr->allFree[possiblePosition].possibilities[j + 1].end;
            }
            allNodesPtr->allFree[possiblePosition].length--;
            allNodesPtr->allFree[possiblePosition].length--;
            break;
        }
    }

    int posToRemove = 0;
    for (posToRemove = 0; posToRemove < allocationsPtr->length; posToRemove++) {
        if (allocationsPtr->allocs[posToRemove].start == allocatedBlock.start) {
            break;
        }
    }
    allocationsPtr->allocs[posToRemove].start = 0;
    allocationsPtr->allocs[posToRemove].end = 0;
    for (; posToRemove < allocationsPtr->length; posToRemove++) {
        allocationsPtr->allocs[posToRemove].start = allocationsPtr->allocs[posToRemove + 1].start;
        allocationsPtr->allocs[posToRemove].end = allocationsPtr->allocs[posToRemove + 1].end;
    }
    allocationsPtr->length--;

    for (int j = 0; j < allNodesPtr->length; j++) {
        int sum = 0;
        for (int k = 0; k < allNodesPtr->allFree[j].length; k++) {
            int size = (allNodesPtr->allFree[j].possibilities[k].end - allNodesPtr->allFree[j].possibilities[k].start) + 1;
            sum += size;
        }
        if (sum == pow(2, j + 1)) {
            int start = allNodesPtr->allFree[j].possibilities[0].start;
            for (int k = 1; k < allNodesPtr->allFree[j].length; k++) {
                if (allNodesPtr->allFree[j].possibilities[k].start < start) {
                    start = allNodesPtr->allFree[j].possibilities[k].start;
                }
            }
            for (int k = 0; k < allNodesPtr->allFree[j].length; k++) {
                allNodesPtr->allFree[j].possibilities[k].start = 0;
                allNodesPtr->allFree[j].possibilities[k].end = 0;
                allNodesPtr->allFree[j].length--;
            }
            allNodesPtr->allFree[j+1].possibilities[allNodesPtr->allFree[j+1].length].start = start;
            allNodesPtr->allFree[j+1].possibilities[allNodesPtr->allFree[j+1].length].end = start + sum -1;
            allNodesPtr->allFree[j+1].length++;
            allNodesPtr->allFree[j].length--;
            break;
        }
    }
    sem_post(sem);
}

int sbmem_close() {
    sem_wait(sem);
    struct TableInfo *tableInfoPtr = (struct TableInfo*) ptr;
    for (int i = 0; i < MAX_NUMBER_OF_PROCESSES; i++) {
        if (tableInfoPtr->pids[i] == getpid()) {
            tableInfoPtr->pids[i] = 0;
            for (int j = i; j < MAX_NUMBER_OF_PROCESSES - 1; j++) {
                tableInfoPtr->pids[j] = tableInfoPtr->pids[j + 1]; 
            }
            break;
        }
    }
    munmap(NULL, tableInfoPtr->allocatableSize + tableInfoPtr->bookKeepingSize);
    sem_post(sem);
    sem_close(sem);
    return 0;
} 
