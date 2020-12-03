#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
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

#define NUMBER_OF_CBBUM       (3) //TODO: don't hardcode, tbd
#define SEM_PROCESS_SHARED    (1)
#define SEMAPHORE_COUNT_MUTEX (1)

// This structure is shared between the BBUM and CCBUM(s).
// It resides in shared memory region created by mmap.
typedef struct 
{
  sem_t pid_arrSem;                          // Semaphore to this structure
  bool  cbbum_instantiated[NUMBER_OF_CBBUM]; // Right after a CBBUM process is
                                             // started It will read this array,
                                             // it will find the first index
                                             // which is not "taken" and assume
                                             // that CBBUMs identity.
                                             // This way at after each CBBUM
                                             // process is forked, it will
                                             // dynamically be given a
                                             // unique Identity.
  int   pid_arr[NUMBER_OF_CBBUM];            // Index of this array is a CBBUM,
                                             // each index will hold the PID of
                                             // the CBBUM with that ID.
} sharedMmap_t;


// Not a production function, just for a brief test.
// returns a random number between 0 and 0xF
int get_random
(
void
)
{
  uint8_t r_data;
  FILE *fp;
  fp = fopen( "/dev/urandom", "r" );
  fread( &r_data, 1, 1, fp );
  fclose( fp );
  return r_data & 0xf;
}

int main
(
    int         argc,
    const char *argv[]
)
{
    int            rc = -1;
    void           *map_start = NULL;
    const size_t   MEM_MAP_SIZE  = sizeof( sharedMmap_t );

    // Initialize a shared memory region which will be passed down to child
    // processes (using fork()), this will let us share a common structure
    // between all the CBBUM(s) and single BBUM.
    map_start = mmap(NULL,
                     MEM_MAP_SIZE,
                     PROT_READ  | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, // Share the mapping with
                                                 // child processes.
                     -1,                         // fd       (don't care)
                     0                           // off_t    (don't care)
                     );
    if ( map_start == MAP_FAILED) 
    {
        //TLE( "Failed to initialize semaphore" );
        exit(0);
    }

    sharedMmap_t * smap_p = (sharedMmap_t*)map_start;
    memset( smap_p, 0, sizeof(sharedMmap_t) );

    // Create a semaphore to be shared between processes (CBBUM(s) + BBUM)
    rc = sem_init( &smap_p->pid_arrSem,
                   SEM_PROCESS_SHARED,    // If this value is non-zero,
                                          // then the semaphore is to be shared
                                          // between processes (our case);
                  SEMAPHORE_COUNT_MUTEX   // Semaphore count,
                                          // 1 == mutex (our case);
                  );
    if( rc == -1 )
    {
        //TLE( "Failed to initialize semaphore" );
        exit( 0 );
    }

    //Spawn multiple CBBUM(s) processes
    pid_t pid;
    for( int i = 0; i < NUMBER_OF_CBBUM; i++ )
    {
        pid = fork();

        //if child process, break and being CBBUM work;
        if( pid == 0 )
        {
             break;
        }
    }

    // BBUM will run this code, it will monitor the children that it started.
    // If any of the CBBUM(s) crash (exit), it will restart that particular
    // CBBUM. This code needs to be extended to include more rigorous logging.
    // and negative case handling.
    // TODO: handle the case that BBUM crashes
    if( pid )
    {
         while( 1 )
         {
             // will block till a child process exits and is read to be reaped.
             int reaped_pid = wait( NULL );

             // check to see if we had an error on wait
             if( reaped_pid  == -1 )
             {
                  if( errno == ECHILD )
                  {
                      printf( "unexpected, should have spawned the CBBUM(s). \n" );
                      exit( -1 );
                  }
                  else
                  {
                      printf( "unexpected error on wait, errno==%s \n",
                          strerror( errno ) );
                      exit( -2 );
                  }
             }

             // get the unique ID of the CBUM that just exited, we can do this by
             // doing a reverse search on the pid_arr since it is indexed with the
             // ID of the CBBUMs.
             sem_wait( &smap_p->pid_arrSem );
             int exited_cbbum_id = -1;

             for( int i = 0; i < NUMBER_OF_CBBUM; i++ )
             {
                  if( reaped_pid == smap_p->pid_arr[i] )
                  {
                       exited_cbbum_id = i;
                  }
             }
             if ( exited_cbbum_id == -1 )
             {
                  printf( "Child process exited, but unable to match to CBBUM id" );
             }

             // Mark this CBBUM as "non-instantiated" and start a new process
             // to assume the exited CBBUM identity.
             smap_p->cbbum_instantiated[exited_cbbum_id] = false;

             printf( "CBBUM %d exited!, had PID == %d, Will restart this CBBUM \n",
             exited_cbbum_id, smap_p->pid_arr[exited_cbbum_id] );
            
             sem_post( &smap_p->pid_arrSem );

             // Fork a new CBBUM
             if( fork() == 0 )
             {
               // CBBUM process, break and enter CBBUM loop
               break;
             }
         }
    }

    // CBBUMs will run this code, register their PIDs to the global structure
    // which is created in the MMAP, run for a bit, then exit.
    {
         sem_wait( &smap_p->pid_arrSem );
         int cbbum_id = 0;

         // find first CBBUM which is not initialized.
         while( smap_p->cbbum_instantiated[cbbum_id]
             && cbbum_id != NUMBER_OF_CBBUM )
         {
              cbbum_id++;
         }
         if ( cbbum_id == NUMBER_OF_CBBUM)
         {
              printf( "CBBUM process started but failed to assume unique ID \n" );
              sem_post( &smap_p->pid_arrSem );
              exit( -1 );
         }

         // Claim our spot in "cbbum_intantiated", this will also
         // set the unique ID of the CBBUM, also update the pid_arr
         // such that our parent knows the new CBBUM child PID.
         smap_p->cbbum_instantiated[cbbum_id] = true;
         smap_p->pid_arr[cbbum_id] = getpid();

         sem_post( &smap_p->pid_arrSem );

         // "worker loop of CBBUM" ; TBD
         while( 1 )
         {
              int timeToSleep = get_random();
              printf( "CBBUM with PID == %d, id == %d will sleep for \
                   %d seconds then exit \n", getpid(), cbbum_id, timeToSleep );
              sleep( timeToSleep );
              exit( 0 );
        }
    }
    return 1;
}
