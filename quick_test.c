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

#define NUMBER_OF_CBBUM       (3)
#define SEM_PROCESS_SHARED    (1)
#define SEMAPHORE_COUNT_MUTEX (1)

// This structure is shared between the BBUM and CCBUM(s).
// It resides in shared memory region created by mmap.
typedef struct {
  sem_t pidArrSem;                           // Semaphore to this structure
  bool  cbbum_instantiated[NUMBER_OF_CBBUM]; // Right after a CBBUM process is started It will read this array,
                                             // it will find the first index which is not "taken" and assume that CBBUM
                                             // Identity.This way at boot each CBBUM will dynamically be given a unique Identity.
  int   pidArr[NUMBER_OF_CBBUM];             // Index of this array is a CBBUM, each index will hold the PID of the CBBUM with that ID.
} sharedMmap_t;


// Not a production function, just for a test.
int getRandom()
{
  uint8_t data[1];
  FILE *fp;
  fp = fopen("/dev/urandom", "r");
  fread(&data, 1, 1, fp);
  fclose(fp);
  return (data[0] & 0xf);
}

int main(void)
{
     int            rc = -1;
     void           *map_start = NULL;
     const size_t   MEM_MAP_SIZE  = sizeof(sharedMmap_t);

     // Initialize a shared memory region which will be passed down to child processes (using fork()),
     // this will let us share a common structure between all the CBBUM(s) and singular BBUM.
     map_start = mmap(NULL,
                      MEM_MAP_SIZE,
                      PROT_READ | PROT_WRITE ,            // READ and WRITE permissions set on the region.
                      MAP_SHARED | MAP_ANONYMOUS,         // Share the mapping between child processes, anonymous means that the
                                                          // mapping has no "name"
                      -1,                                 // fd       (don't care, can be "0", some implementations prefer "-1")
                      0                                   // off_t    (don't care)
                      );

     // Create a structure out of this new "memory"
     sharedMmap_t * smap = (sharedMmap_t*)map_start;
     memset(smap, 0, sizeof(sharedMmap_t));

     // Create a semaphore to be shared between processes (CBBUM(s) + BBUM)
     rc = sem_init(&smap->pidArrSem,       // Area in memory that the shared semaphore exists
                   SEM_PROCESS_SHARED,     // If this value is non-zero, then the semaphore is to be shared between processes (our case);
                   SEMAPHORE_COUNT_MUTEX   // Semaphore count, 1 == mutex (our case);
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

     // BBUM will run this code, it will monitor the children that it started.
     // If any of the CBBUM(s) crash (exit), it will restart that particular CBBUM.
     // This code needs to be extended to include more rigorous logging.
     // TODO: handle the case that BBUM crashes
     if(child_pid){
       while(1)
       {
         int reaped_child_pid = wait(NULL);

          // check to see if we had an error on wait 
          if(reaped_child_pid  == -1)
          {
             if(errno == ECHILD)
             {
              printf("unexpected, should have spawned the CBBUM(s). \n");
              exit(-1);
             }
             else
             {
              printf("unexpected error on wait! errno ==  %s \n", strerror(errno));
              exit(-2);
             }
         }

         // get the unique ID of the CBUM that just exited, we can do this by
         // doing a reverse search on the pidArr since it is indexed with the ID of 
         // the CBBUMs. this is not "Ideal" - but since the number of CBBUMs is small it wil
         // do.

         sem_wait(&smap->pidArrSem);
         int exited_cbbum_id = -1;

         for(int i = 0; i < NUMBER_OF_CBBUM; i++)
         {
           if(reaped_child_pid == smap->pidArr[i])
           {
              exited_cbbum_id = i;
           }
         }
         //TODO handle case that we don't find exited_cbbum_id

         // Mark this CBBUM as "non-instantiated", such that it is restarted.
         smap->cbbum_instantiated[exited_cbbum_id] = false;
         sem_post(&smap->pidArrSem);

         printf("CBBUM %d exited!, had PID == %d, Will restart this BBUM \n", exited_cbbum_id, smap->pidArr[exited_cbbum_id]);

         // Fork a new CBBUM
         // TODO: handle negative case (fork returns -1)
         if(fork() == 0)
         {
           // CBBUM process, will break and enter CBBUM loop
           break;
         }
       }
     }

     // CBBUMs will run this code, register their PIDs to the global structure
     // which is created in the MMAP, run for a bit, then exit.
     {
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

       // "worker loop of CBBUM" ; TBD
       while(1){
          int timeToSleep = getRandom();
          printf("CBBUM with PID == %d, id == %d will sleep for %d seconds then exit \n", getpid(), cbbum_id, timeToSleep);
          sleep(timeToSleep);

          exit(0);
       }
     }
     
  return 1;
}
