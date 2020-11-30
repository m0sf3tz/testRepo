#include  <stdio.h>
#include  <string.h>
#include  <sys/types.h>
#include  <fcntl.h>            /* Defines O_* constants */
#include  <sys/stat.h>         /* Defines mode constants */
#include  <sys/mman.h>
#include  <stdint.h>
#include  <semaphore.h>
#include  <sys/types.h>
#include  <sys/wait.h>
#include  <string.h>
#include  <errno.h>

#define NUMBER_OF_BBUMS       (11)
#define SEM_PROCESS_SHARED    (1)
#define SEMAPHORE_COUNT_MUTEX (1)

typedef struct {
  sem_t pidArrSem; 
  int   pidArr[NUMBER_OF_BBUMS];
} sharedMmap_t; 


int getRandom(){
  uint8_t data[1];
  FILE *fp;
  fp = fopen("/dev/urandom", "r");
  fread(&data, 1, 1, fp);
  fclose(fp);
  return (data[0] & 0xf);
}

void  main(void)
{
     int            rc = -1; 
     pid_t          pid;
     void           *map_start = NULL; 
     const size_t   page_size  = 2<<12; //this must be page aligned to the MMU granularity,
                                        //this should be 4K in both ARM and x86 I think
     
     //Initilaize a shared memory region which will be passed done to children
     //this will let us share semaphores and data between BBUM and BBUMS
     map_start = mmap(NULL,
                 page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC, //
                 MAP_SHARED | MAP_ANONYMOUS,         //Share the mapping between different children    
                 NULL,                               //fd       (don't care)
                 NULL);                              //off_t    (don't care)
  

     //create a structure out of this new "memory"
     sharedMmap_t * smap = (sharedMmap_t*)map_start;
     
     //empty out the shared memory
     memset(smap,sizeof(sharedMmap_t),0); 

     //create a semaphore to be shared between processes
     rc = sem_init(&smap->pidArrSem,       //Area in memory that the shared semaphore exists;
                   SEM_PROCESS_SHARED,     //If this value is non-zero, then the semaphore is to be shared between processes;
                   SEMAPHORE_COUNT_MUTEX   //semaphore count, 1 == mutex;
                   );
     if(rc == -1){
         printf("Failed to init anon semaphore \n");
     }

     //Spawn multiple BBUMs 
     for(int i = 0; i < NUMBER_OF_BBUMS; i++){
        pid = fork();
        if(pid == 0) //if child, break;
          break;
     }

     //BBUM will run this code, it will monitor the children that it started 
     //last PID created not zero, parent left loop above
     //this does not use the semaphore, since the spawned processes should have finished updating the shared
     //global mmap, ofcourse, this is only valid for this simple example...
     if(pid){
       printf("Hello from parent, i'm done spawning children! \n");
       sleep(1);

       printf("here are my children!\n");
       for(int i = 0; i < NUMBER_OF_BBUMS; i++){
        printf("BBUMS %i is being tracked with PID == %d \n", i, smap->pidArr[i]);
       }

       while(1){
         int wstatus;
         int pid_dead_child = wait(NULL);
         if(pid_dead_child == -1){
             if(errno == ECHILD)
              printf("all children have been reaped, will exit myself! Here is ERRNO %s \n", strerror(errno));
             else
              printf("unexpected error on errno...? %s \n", strerror(errno));
             
             exit(0);
         }

         printf("BBUMS with PID = %d exited! \n", pid_dead_child); 

         for(int i = 0; i < NUMBER_OF_BBUMS; i++){
           if(pid_dead_child == smap->pidArr[i])
             printf("BBUMS %d was killed, he had PID == %d, I will do something now (restart this BBUM) \n", i, smap->pidArr[i]);
        }
      }
     }


     // BBUMS will run this code, register their PIDs to the global MMAP, run for a bit, then exit.
     // only children get here, update talbe with PID
     sem_wait(&smap->pidArrSem);
     int slot = 0;
     
     //find first empty slot
     while(smap->pidArr[slot]){
      slot++;
     }
     smap->pidArr[slot] = getpid();
     sem_post(&smap->pidArrSem);
    
     while(1){
        int timeToSleep = getRandom();
        printf("BBUMS with PID == %d will sleep for %d then exit \n", getpid(), timeToSleep);
        sleep(timeToSleep);

        exit(0);
     }
}
