#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>            /* Defines O_* constants */
#include <sys/stat.h>         /* Defines mode constants */
#include <sys/mman.h>
#include <stdint.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h> 
#include <stdlib.h>
#include <unistd.h>

#define NUMBER_OF_CBBUM       (11)
#define SEM_PROCESS_SHARED    (1)
#define SEMAPHORE_COUNT_MUTEX (1)

// This structure is shared between the BBUM and CCBUM(s).
// It resides in shared memory 
typedef struct {
  sem_t pidArrSem;                           // Semaphore to this structure
  bool  cbbum_instantiated[NUMBER_OF_CBBUM]; // If a particular CBBUM is instantiated. Right after a CBBUM process is started
                                             // It will read this array, it will find the first index which is not "taken".
                                             // This way at boot each CBBUM will dynamically be given a unique Identity.
  int   pidArr[NUMBER_OF_CBBUM];             // Index of this array is a CBBUM, it will store the PID of the child process.
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
     void           *map_start = NULL;
     const size_t   MEM_MAP_SIZE  = sizeof(sharedMmap_t);

     // Initialize a shared memory region which will be passed down to child processes,
     // this will let us share a common structure between all the processes.
     map_start = mmap(NULL,
                      MEM_MAP_SIZE,
                      PROT_READ | PROT_WRITE ,            // READ and WRITE permissions set on the region.
                      MAP_SHARED | MAP_ANONYMOUS,         // Share the mapping between child processes, anonymous means that the
                                                          // mapping has no "name"
                      0,                                  // fd       (don't care)
                      0                                   // off_t    (don't care)
                      );

     // Create a structure out of this new "memory"
     sharedMmap_t * smap = (sharedMmap_t*)map_start;

     // Empty out the shared memory
     memset(smap,sizeof(sharedMmap_t),0);

     // Create a semaphore to be shared between processes
     rc = sem_init(&smap->pidArrSem,       // Area in memory that the shared semaphore exists
                   SEM_PROCESS_SHARED,     // If this value is non-zero, then the semaphore is to be shared between processes (our case);
                   SEMAPHORE_COUNT_MUTEX   // Semaphore count, 1 == mutex (our care);
                   );
     if(rc == -1){
         printf("Failed to create anon-semaphore \n");
         exit(0);
     }

     //Spawn multiple CBBUM(s)
     pid_t child_pid;
     for(int i = 0; i < NUMBER_OF_CBBUM; i++){
        child_pid = fork();

        //if child, break;
        if(child_pid == 0)
        {
          break;
        }
     }

     //BBUM will run this code, it will monitor the children that it started 
     //this does not use the semaphore, since the spawned processes should have finished updating the shared
     //global mmap, ofcourse, this is only valid for this simple example...
     if(child_pid){
       printf("Finished spawning children, will sleep a bit \n");

       //TODO: HACK to get us going, assuming all threads will be spawned in 10 seconds,
       //will update logic after flushing out system
       sleep(10);

       printf("Here are the spawned CBBUMs \n");

       for(int i = 0; i < NUMBER_OF_CBBUM; i++)
       {
        printf("CBBUM %i is being tracked with PID == %d \n", i, smap->pidArr[i]);
       }

       // We will loop and wait for each thread to exit, 
       // after doing so, we will reap the child process 
       // in the real case, we would do error checking here and restart the child
       // CBBUM that exited, we want to keep code here as simple as possible since 
       // crashing here would make recovering very difficult - this case is not currently considered

       while(1){
         int wstatus;
         int reaped_child_pid = wait(NULL);

          // check to see if we had an error on wait 
          if(reaped_child_pid  == -1){
             if(errno == ECHILD)
             {
              printf("all children have been reaped, BBUM process exiting \n");
              exit(0);
             }
             else
             {
              printf("unexpected error on wait! errno ==  %s \n", strerror(errno));
              exit(-1);
             }
         }

         // get the unique ID of the CBUM that just exited, we can do this by
         // doing a reverse search on the pidArr since it is indexed with the ID of 
         // the CBBUMs. this is not "Ideal" - but since the number of CBBUMs is small it wil
         // do.
         printf("CBBUM with PID = %d exited! \n", reaped_child_pid ); 
         int exited_cbbum_id = -1; 

         for(int i = 0; i < NUMBER_OF_CBBUM; i++)
         {
           if(reaped_child_pid == smap->pidArr[i])
           {
              exited_cbbum_id = i;
           }
         }

         printf("BBUMS %d was killed, had PID == %d, TODO: restart this BBUM \n", exited_cbbum_id, smap->pidArr[exited_cbbum_id]);
       }
     }

     // CBBUMs will run this code, register their PIDs to the global structure
     // which is created in the MMAP, run for a bit, then exit.
     // only CBBUMS will run this code,
     sem_wait(&smap->pidArrSem);
     int cbbum_id = 0;

     // find first cbbum which is not initialized.
     while(smap->cbbum_instantiated[cbbum_id]){
      cbbum_id++;
     }

     // Claim our spot in "cbbum_intantiated", this will also
     // set the unique ID of the CBBUM, also let update the pidArr
     // such that our parent knows our PID.
     smap->cbbum_instantiated[cbbum_id] = true;
     smap->pidArr[cbbum_id] = getpid();

     sem_post(&smap->pidArrSem);

     while(1){
        int timeToSleep = getRandom();
        printf("BBUMS with PID == %d will sleep for %d then exit \n", getpid(), timeToSleep);
        sleep(timeToSleep);

        exit(0);
     }
}
