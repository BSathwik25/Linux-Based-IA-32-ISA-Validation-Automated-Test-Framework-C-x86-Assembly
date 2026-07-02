
//
// simple encoding example fr IA-32 validation project
//
// Project Part3: added multple processing support
//
#define _GNU_SOURCE // Required for advanced CPU features like affinity
// ---- Headers ----
#include <stdio.h>  // Standard I/O
#include <stdlib.h> // General utilities- malloc, exit, rand, etc.
#include <errno.h>  // Error codes
#include <sys/types.h> // pid_t, etc.
#include <sys/mman.h>  // mmap()

#include <limits.h>    /* for PAGESIZE */
#include <sched.h>     // CPU binding

#include "ia32_encode.h" // Custom header file that provides instruction encoding utilities

// --- Threading and synchronization ---
#include <semaphore.h>  // Semaphores
#include <unistd.h>     // Fork, sleep, etc.
#include <sys/wait.h>   // waitpid
#include <stdint.h>     // Fixed-size types

// --- Global variables for barrier synchronization (used by threads/processes) ---
volatile int *barrier_counter;        // Keeps track of how many threads have reached the barrier
sem_t *barrier_lock;                  // Lock to control access to shared counter
sem_t *barrier_mutex;                 // Mutex to ensure exclusive access
sem_t *barrier_sem;                   // Threads will wait on this until all reach the barrier

// --- Function declarations ---
int bind_to_CPU(int thread_id, pid_t pid);  // Assigns process to a specific CPU
int build_instructions(volatile char *next_ptr, int thread_id, int icount, int seed, FILE *fp);

// --- Shared memory pointers ---
volatile char *mptr = 0, *next_ptr = 0, *mdptr = 0, *comm_ptr = 0;

// --- Global control variables ---
int num_inst = 0;                      
int i = 0;                             
int target_ninstrs = MAX_DEF_INSTRS;  
int nthreads = 1;                      
int pid_task[MAX_THREADS], pid = 0;   
int temp = 0;                          
volatile int *turn_ptr = NULL;        

// --- Structure to hold memory-mapped pointer ---
typedef struct { 
    volatile unsigned long *pointer_addr;  
} test_i;

test_i test_info[NUM_PTRS];  

// --- Per-thread pointer arrays ---
typedef volatile unsigned long *tptrs;

volatile char *mptr_threads[MAX_THREADS];           
volatile unsigned long *mdptr_threads[MAX_THREADS]; 
volatile unsigned long *comm_ptr_threads[MAX_THREADS]; 

// 
// declarations for starting test
//
typedef int (*funct_t)();
funct_t start_test;
int executeit();

 static inline volatile char *add_headeri(volatile char *target_address);
 static inline volatile char *add_endi(volatile char *target_address);
 static inline volatile char *enter( volatile char *target_address, int imm);
 static inline volatile char *leave(volatile char *target_address);
 static inline volatile char *ret(volatile char *target_address);
 
 void wait_for_barrier(int thread_id, int total_threads); // Barrier sync for multiple threads

 
#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/*
 * simple routine to randomize numbers in a range
 */
int rand_range(int min_n, int max_n)
{
	return rand() % (max_n - min_n + 1) + min_n;
}

long data_addr; 
FILE *fp = NULL;

int main(int argc, char *argv[])
{
 // --- Argument parsing ---
   if (argc < 5) {
        fprintf(stderr, "Usage: %s <icount> <seed> <nthreads> <logfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
	
	int seed;
	int ibuilt=0;
  int icount;
	char* logfile = NULL;
	
    icount = atoi(argv[1]);         // input for random generation output
	seed = atoi(argv[2]);		    // input seed value
	nthreads = atoi(argv[3]);      // Number of threads/processes
	logfile = argv[4];

        printf("Arguments received:\n");
        printf("icount = %d\n", icount);
  	    printf("seed = %d\n", seed);
        printf("nthreads = %d\n", nthreads);
        printf("logfile = %s\n", logfile);
		
        if (logfile == NULL || *logfile == '\0') {
        fprintf(stderr, "Invalid log file path\n");
        exit(EXIT_FAILURE);
    
         }
	
	/* process arguments here */

	printf("\nstarting seed = %d\n",seed);

	/* need number of instructions */
	
	fp = fopen(logfile, "w");
      if (fp == NULL) {
      perror("Could not open logfile");
      fprintf(stderr, "Failed to open log file: %s\n", logfile);
      exit(EXIT_FAILURE);
      } 

	printf("icount = %d\n", icount);
	printf("starting seed = %d\n", seed);
	printf("nthreads = %d\n", nthreads);
	
	
	/* need to pass in # of threads */

    // Limit threads to MAX_THREADS
	if (nthreads > MAX_THREADS) {
		fprintf(stderr,"Sorry only built for %d threads over riding your %d\n", MAX_THREADS, nthreads);
		fflush(stderr);
		nthreads=MAX_THREADS;
	}

	srand(seed); // set random seed


	/* allocate buffer to perform stores and loads to  */
        test_info[DATA].pointer_addr = mmap(
		(void *) 0,
		(MAX_DATA_BYTES+PAGESIZE-1) * nthreads,
		PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED,
		0, 0
		);

	/* save the base address for debug like before */

	mdptr=(volatile char *)test_info[DATA].pointer_addr;

	if (((int *)test_info[DATA].pointer_addr) == (int *)-1) {
		perror("Couldn't mmap (MAX_DATA_BYTES)");
		exit(1);
	}

	/* allocate buffer to build instructions into */

	test_info[CODE].pointer_addr = mmap(
		(void *) 0,
		(MAX_INSTR_BYTES+PAGESIZE-1) * nthreads,
		PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_ANONYMOUS | MAP_SHARED,
		0, 0
		);

	/* keep a copy to the base here */

	mptr=(volatile char *)test_info[CODE].pointer_addr;
	
    // Set page permissions
	if (mprotect((void *)mptr, (MAX_INSTR_BYTES + PAGESIZE - 1) * nthreads, PROT_READ | PROT_EXEC | PROT_WRITE) == -1) {
    perror("mprotect failed on mptr");
    exit(1);
    }

    data_addr=(long)mdptr;

	/* allocate buffer to build communications area into */

    test_info[COMM].pointer_addr = mmap(
    (void *) 0,
    (sizeof(int) * 2) + sizeof(sem_t) * 3,  // enough for 2 ints + 3 semaphores
    PROT_READ | PROT_WRITE,
    MAP_ANONYMOUS | MAP_SHARED,
    -1, 0
    );
    comm_ptr = (volatile char *)test_info[COMM].pointer_addr;

    if (((int *)test_info[COMM].pointer_addr) == (int *)-1) {
    perror("Couldn't mmap (MAX_COMM_BYTES)");
    exit(1);
    }
  // Assign shared barrier pointers
  barrier_counter = (int *)comm_ptr;
  turn_ptr = (int *)(comm_ptr + sizeof(int));
  barrier_lock = (sem_t *)(comm_ptr + 2 * sizeof(int));
  barrier_mutex = (sem_t *)(comm_ptr + 2 * sizeof(int) + sizeof(sem_t));
  barrier_sem   = (sem_t *)(comm_ptr + 2 * sizeof(int) + 2 * sizeof(sem_t));
   
  // Initialize shared values and semaphores
  *barrier_counter = 0;
  *turn_ptr = 0;
  sem_init(barrier_lock, 1, 1);
  sem_init(barrier_mutex, 1,1);
  sem_init(barrier_sem, 1,0);


	/* make the standard output and stderrr unbuffered */

	setbuf(stdout, (char *) NULL);
	setbuf(stderr, (char *) NULL);


	/* start appropriate # of threads */

	for (i=0;i<nthreads;i++) 
	{ 
        next_ptr=(mptr+(i*MAX_INSTR_BYTES));          // init next_ptr
		fprintf(stderr,"T%d next_ptr=0x%lx\n",i,(unsigned long)next_ptr);
		// Init thread-specific pointers
		mdptr_threads[i]=(tptrs)(mdptr+(i*MAX_DATA_BYTES));  // init thread data pointer
		mptr_threads[i]=(volatile char *)next_ptr;                     // save pointer per thread
   fprintf(stderr, "DEBUG: mptr_threads[%d] = 0x%lx\n", i, (unsigned long)mptr_threads[i]);

   // Align to 16 bytes for safety
       mptr_threads[i] = (volatile char *)(((uintptr_t)mptr_threads[i] + 15) & ~0xF);
       comm_ptr_threads[i]=(tptrs)comm_ptr;                 // everyone gets the same for now


		/* use fork to start a new child process */

    if((pid=fork()) == 0) {
    fprintf(stderr,"T%d fork\n",i);
    fflush(stderr);

    //  Open logfile in APPEND mode inside child
        fp = fopen(logfile, "a");
        if (fp == NULL) {
        perror("Could not open logfile");
        exit(EXIT_FAILURE);
        }

    //  Bind child process to a CPU
    int x = bind_to_CPU(i, getpid());
    if (x != 0) exit(1);
    // Wait for our turn to write to logfile.txt
    while (*turn_ptr != i) {
    sched_yield();  // Be nice to CPU: yield if it's not our turn
    }

    //  Build and execute instructions
    ibuilt = build_instructions(mptr_threads[i], i, icount, seed, fp);
	
    fprintf(stderr, "T%d waiting at barrier\n", i);
    wait_for_barrier(i, nthreads);
    fprintf(stderr, "T%d passed barrier, starting execution\n", i);
	
	// Execute generated code
    start_test = (funct_t) mptr_threads[i];
    if (start_test == NULL) {
    fprintf(stderr, "T%d: start_test is NULL\n", i);
    exit(EXIT_FAILURE);
    }
    if ((uintptr_t)start_test < 0x1000 || (uintptr_t)start_test > 0x7fffffffffff) {
    fprintf(stderr, "T%d: Invalid start_test address: 0x%lx\n", i, (long)start_test);
    exit(1);
    }

    executeit(start_test);
    
    // Done writing to logfile, now allow next thread to proceed
    (*turn_ptr)++;

    // Write footer log
    fprintf(stderr,"T%d generation program complete, instructions generated: %d\n",i, ibuilt);
    fflush(stderr);

    //  Close the logfile safely
    
    fclose(fp);
    fp = NULL;
    break;
    }	
      else if (pid_task[i] == -1) {
			perror("fork me failed");
			exit(1);
		} else { // this should be the parent 

			pid_task[i]=pid; // save pid

			fprintf(stderr,"child T%d started:\n",pid);
			fflush(stderr);

		}
	     
	} // end for nthreads


	// wait for threads to complete

	for (i=0;i<nthreads;i++) {
		waitpid(pid_task[i], NULL, 0);
	}


	// clean up the allocation before getting out

	munmap((caddr_t)mdptr,(MAX_DATA_BYTES+PAGESIZE-1)*nthreads);
	munmap((caddr_t)mptr,(MAX_INSTR_BYTES+PAGESIZE-1)*nthreads);
	munmap((caddr_t)comm_ptr, (sizeof(int) * 2 + sizeof(sem_t) * 3));
   if(fp != NULL) fclose(fp); // Ensure the log file is properly closed
        
   return 0;

   }

/*
 * Function: executeit
 * 
 * Description:
 *
 * This function will start executing at the function address passed into it 
 * and return an integer return value that will be used to indicate pass(0)/fail(1)
 *
 * INTPUTs:  funct_t start_addr :      function pointer 
 *
 * Returns:  int                :      0 for pass, 1 for fail
 */   
int executeit(funct_t start_addr) 
{

	volatile int rc=0;

	i=0;

	rc=(*start_addr)();

	return(0);
}

//
// Routine:  build_instructions
//
// Description:
//
// INPUT: none yet
// 
// OUTPUT: returns the number of instructions built
// 
    int build_instructions(volatile char *next_ptr, int thread_id, int icount, int seed, FILE *fp){

	int num_inst=0;
	
	// Log to stderr which thread is building instructions
    fprintf(stderr, "building instructions for T%d\n",thread_id);
	fflush(stderr);
    int i;
    // Add prologue/header instructions
	next_ptr=add_headeri((volatile char *)next_ptr);
    
	next_ptr = build_imm_to_register(ISZ_8, (long)mdptr_threads[thread_id], REG_EAX, next_ptr);

    for (i=0; i<icount; i++)
	{
		if ((next_ptr - mptr_threads[thread_id]) >= MAX_INSTR_BYTES - 64) {
        fprintf(stderr, "T%d: next_ptr overflow, stopping instruction generation early.\n", thread_id);
        break;
    }
		long immediate_value;
		long  displacement;
		int inst_type,mov_size;
		int src_reg,dest_reg; 
		
		// Randomization of inputs
		inst_type= rand_range(1,9);
		src_reg= rand_range(0,7);				// Generate random registers 
		dest_reg= rand_range(0,7);
		mov_size= rand_range(1,4);				// specifies the inst size
		immediate_value=rand_range(20,500);			// generates random immediate_value value within 20 - 500
		displacement= rand_range (0x10,0x2000);           // generates random memory displacement value within 10- 2000
		
	if (mov_size == 3 || mov_size == 5 || mov_size == 6 || mov_size == 7) {
    mov_size = 4;
    }
 
//		dest_reg=(dest_reg==4||5)?1:dest_reg;   //if dest_Reg is ESP/EBP, then make it ESI 
		dest_reg=(dest_reg==4)?1:dest_reg;    //if dest_Reg is ESP, then make it ECX
		dest_reg=(dest_reg==5)?6:dest_reg;    //if dest_Reg is EBP, then make it ESI
    if (src_reg < 0 || src_reg > 7 || dest_reg < 0 || dest_reg > 7) {
    fprintf(fp, "Invalid register values: src=%d dest=%d (skipping)\n", src_reg, dest_reg);
    continue;  // skip bad instruction
   }

		switch(inst_type)
		{
	case 1: // Move Reg to reg
  //  if (mov_size == 8) break;
    if (src_reg >= 0 && src_reg <= 7 && dest_reg >= 0 && dest_reg <= 7) {
        next_ptr=build_mov_register_to_register(mov_size, src_reg ,dest_reg,next_ptr);
        num_inst++;
        fprintf(fp,"Thread %d...\n Instruction MOV REG to reg:Instruction_size=%d\t src_reg=%d\t dest_reg=%d\n",thread_id,mov_size,src_reg,dest_reg);
        fprintf(fp,"Thread no %d \t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
        fprintf(stderr,"Thread no %d\t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
        fflush(fp);
    }
    break;
	case 2: // Move Imm to reg 
//	if (mov_size == 8) break;
    if (dest_reg >= 0 && dest_reg <= 7) {
		next_ptr=build_imm_to_register(mov_size, immediate_value ,dest_reg, next_ptr);
		num_inst++;
		fprintf(fp,"Thread %d...\n Instruction MOV IMM to reg:Instruction_size=%d\t src_reg=%ld\t dest_reg=%d\n",thread_id,mov_size,immediate_value,dest_reg);
		fprintf(fp,"Thread no %d \t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fprintf(stderr,"Thread no %d\t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fflush(fp);
    }
	break;
	case 3:  //Move Reg to mem
    //if (mov_size == 8) break;
    if (src_reg >= 0 && src_reg <= 7) {
		next_ptr=build_reg_to_memory(mov_size,src_reg,REG_EAX,displacement,next_ptr);
		num_inst++;
		fprintf(fp,"Thread %d...\n Instruction MOV Register to Memory :Inst_size=%d\t Src_reg =%d\t mem_reg=%d\tDisplacement=%ld \t\n", thread_id,mov_size,src_reg,REG_EAX,displacement);
		fprintf(fp,"Thread no %d \t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fprintf(stderr,"Thread no %d\t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fflush(fp);
	}
	break;
	case 4: // Move mem to reg
//	if (mov_size == 8) break;
    if (dest_reg >= 0 && dest_reg <= 7) {
		next_ptr=build_mov_memory_to_reg(mov_size,dest_reg,REG_EAX,displacement, next_ptr);
		num_inst++;
		fprintf(fp,"Thread %d...\n Instruction MOV Memory to Register :Inst_size=%d\t mem_reg =%d\t dest_reg=%d\tDisplacement=%ld \t\n"   ,thread_id, mov_size,REG_EAX,dest_reg,displacement);
		fprintf(fp,"Thread no %d \t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fprintf(stderr,"Thread no %d\t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fflush(fp);
	}
    break;
	case 5: // xadd
	if (src_reg >= 0 && src_reg <= 7 && dest_reg >= 0 && dest_reg <= 7) {
		next_ptr=build_xadd(mov_size,src_reg,dest_reg, next_ptr);
		num_inst++;
		fprintf(fp,"Thread %d...\n Instruction XADD :Inst_size=%d\t scr_reg =%d\t dest_reg=%d\t\t\n",thread_id, mov_size,src_reg,dest_reg);
		fprintf(fp,"Thread no %d \t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fprintf(stderr,"Thread no %d\t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fflush(fp);
	}
    break;
    case 6: // xchng 
	if (src_reg >= 0 && src_reg <= 7 && dest_reg >= 0 && dest_reg <= 7) {
		next_ptr=build_xchng(mov_size,src_reg,dest_reg,next_ptr);
		num_inst++;
		fprintf(fp,"Thread %d...\n Instruction XCHNG :Inst_size=%d\t scr_reg =%d\t dest_reg=%d\t\t\n",thread_id, mov_size,src_reg,dest_reg);
		fprintf(fp,"Thread no %d \t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fprintf(stderr,"Thread no %d\t next ptr is now 0x%lx\n",thread_id, (long) next_ptr);
		fflush(fp);
	}
    break;
	case 7://mfence
	if (next_ptr != NULL) {
		next_ptr = build_mfence(next_ptr);
		num_inst++;
		fprintf(fp, "Thread %d...Instruction MFENCE\t\t", thread_id);
		fprintf(fp, "Thread no %d \t next ptr is now 0x%lx\n", thread_id, (long) next_ptr);
		fprintf(stderr, "Thread no %d\t next ptr is now 0x%lx\n", thread_id, (long) next_ptr);
		fflush(fp);
	}
    break;
    case 8: //lfence
	if (next_ptr != NULL) {
		next_ptr = build_lfence(next_ptr);
		num_inst++;
		fprintf(fp, "Thread %d...:Build instruction LFENCE \t\t", thread_id);
		fprintf(fp, "Thread no %d \t next ptr is now 0x%lx\n", thread_id, (long) next_ptr);
		fprintf(stderr, "Thread no %d\t next ptr is now 0x%lx\n", thread_id, (long) next_ptr);
		fflush(fp);
	}
    break;

    case 9: //sfence
	if (next_ptr != NULL) {
		next_ptr = build_sfence(next_ptr);
		num_inst++;
		fprintf(fp, "Thread %d...:Build instruction SFENCE \t\t", thread_id);
		fprintf(fp, "Thread no %d \t next ptr is now 0x%lx\n", thread_id, (long) next_ptr);
		fprintf(stderr, "Thread no %d\t next ptr is now 0x%lx\n", thread_id, (long) next_ptr);
		fflush(fp);
    }
    break;		
  }	
}
      
    next_ptr = build_imm_to_register(ISZ_8, (long)mdptr, REG_EAX, next_ptr);

	next_ptr=add_endi((volatile char *)next_ptr);
 
    //  Signal barrier: this thread is done, let next proceed
    sem_wait(barrier_lock);
    barrier_counter++;
    sem_post(barrier_lock);


	return (num_inst);
}

int bind_to_CPU(int i,int pid)
{	
		cpu_set_t mask;
		
        CPU_ZERO( &mask );		//initializes all the bits in the mask to zero
		
        CPU_SET( i, &mask );	//sets only the bit corresponding to cpu.
		
		if( sched_setaffinity( pid, sizeof(mask), &mask ) == -1 )	
		{
	      fprintf(fp,"ERROR: Unable to set CPU Affinity.\n");
		  return(-1);
		}
		else {
		fprintf(fp," Assigned child process T%d to CPU_%d\n",pid,i);
		return(0);
}
}
	// INSERT YOUR CODE HERE AND NUKE MINE :-)

int rand_generator( int minimum_n,int maximum_n)
{
	return rand () % (maximum_n-minimum_n +1) + minimum_n;
	
}

// Add function prologue (header) - enter instruction, push some registers
static inline volatile char *add_headeri( volatile char *target_address)
{

	target_address = enter(target_address, 2048);
	

    target_address=build_push_reg(REG_EBX,0,target_address);
    target_address=build_push_reg(MYREG_R12,1,target_address);
  	target_address=build_push_reg(MYREG_R13,1,target_address);
	target_address=build_push_reg(MYREG_R14,1,target_address);
	target_address=build_push_reg(MYREG_R15,1,target_address);
	
	return(target_address);
}

// Build the ENTER instruction (sets up stack frame)
static inline volatile char *enter( volatile char *target_address, int imm)
{
	(*(int*) target_address) = ((0)<< 24) | ((imm )<< 8 )| 0xC8;
	target_address += BYTE4_OFF;
	return(target_address);
}

// Add function epilogue (pop registers, leave, ret)
/*static inline volatile char *add_endi( volatile char *target_address)
{	
	
	target_address=build_pop_reg(MYREG_R15,1,target_address);
 	target_address=build_pop_reg(MYREG_R14,1,target_address);
	target_address=build_pop_reg(MYREG_R13,1,target_address);
	target_address=build_pop_reg(MYREG_R12,1,target_address);	
    target_address=build_pop_reg(REG_EBX,0,target_address);
		
	target_address = leave(target_address);
	target_address = ret(target_address);
	
	return(target_address);
}
*/
static inline volatile char *add_endi( volatile char *target_address)
{
    target_address = build_mfence(target_address);  // Optional sync hint
    target_address = build_lfence(target_address);  // Optional sync hint

    // Epilogue
    target_address = build_pop_reg(MYREG_R15,1,target_address);
    target_address = build_pop_reg(MYREG_R14,1,target_address);
    target_address = build_pop_reg(MYREG_R13,1,target_address);
    target_address = build_pop_reg(MYREG_R12,1,target_address);
    target_address = build_pop_reg(REG_EBX,0,target_address);

    target_address = leave(target_address);
    target_address = ret(target_address);

    return target_address;
}

// Build LEAVE instruction (tears down stack frame)
static inline volatile char *leave(volatile char *target_address)
{
	
	(*(char*) target_address) = 0xC9;
	target_address += BYTE1_OFF;
	return(target_address);
}

// Build RET instruction (returns from function)
static inline volatile char *ret(volatile char *target_address)
{
    (*(char*) target_address) = 0xC3;
	target_address += BYTE1_OFF;
	return(target_address);
} 

// Wait for all threads to reach barrier synchronization point
void wait_for_barrier(int thread_id, int total_threads) {
    sem_wait(barrier_mutex);                // Lock
    (*barrier_counter)++;                   // Increment count
    sem_post(barrier_mutex);                // Unlock

    if (*barrier_counter == total_threads) {
        // Last thread arrived: release all
        for (int i = 0; i < total_threads; i++) {
            sem_post(barrier_sem);          // Post once per thread
        }
    }

    // Wait until all threads reach barrier
    sem_wait(barrier_sem);
}


