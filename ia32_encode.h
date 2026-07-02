//encodeit.c

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>

#include "ia32_encode.h"

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

// globals to aid debug to start
volatile char *mptr = 0, *next_ptr = 0, *mdptr = 0;
int num_inst = 0;

// declarations for starting test
typedef int (*funct_t)();
funct_t start_test;
int executeit();

// Global config variables
unsigned seed;
int num_instructions;
int build_instructions();
int main(int argc, char *argv[])
{
    int ibuilt = 0, rc = 0;

    if (argc != 3) {
        printf("Usage: %s <seed> <num_instructions>\n", argv[0]);
        exit(1);
    }

    seed = (unsigned)atoi(argv[1]);
    num_instructions = atoi(argv[2]);

    /* allocate buffer to perform stores and loads to, and set permissions  */
    mdptr = (volatile char *)mmap(
        (void *)0,
        (MAX_DATA_BYTE + PAGESIZE - 1),
        PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED,
        0, 0);

    if (mdptr == MAP_FAILED) {
        printf("data mptr allocation failed\n");
        exit(1);
    }

    /* allocate buffer to build instructions into, and set permissions to allow execution of this memory area */
    mptr = (volatile char *)mmap(
        (void *)0,
        (MAX_INSTR_BYTES + PAGESIZE - 1),
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_ANONYMOUS | MAP_SHARED,
        0, 0);

    if (mptr == MAP_FAILED) {
        printf("instr  mptr allocation failed\n");
        exit(1);
    }

    next_ptr = mptr; // init next_ptr
    ibuilt = build_instructions(); // build instructions

    /* ok now that I built the critters, time to execute them */
    start_test = (funct_t)mptr;
    executeit(start_test);

    fprintf(stderr, "generation program complete, %d instructions generated, and executed\n", ibuilt);

    // clean up the allocation before getting out
    munmap((caddr_t)mdptr, (MAX_DATA_BYTE + PAGESIZE - 1));
    munmap((caddr_t)mptr, (MAX_INSTR_BYTES + PAGESIZE - 1));

    return 0;
}

/*
 * Function: rand_range
 * --------------------
 * Generates a random integer between min_n and max_n (inclusive).
 *
 * Parameters:
 *   min_n - The lower bound of the range.
 *   max_n - The upper bound of the range.
 *
 * Returns:
 *   A random integer in the range [min_n, max_n].
 *   If min_n > max_n, the function swaps the values to ensure a valid range.
 */
int rand_range(int min_n, int max_n)
{
    // Swap if min is greater than max
    if (min_n > max_n) {
        int temp = min_n;
        min_n = max_n;
        max_n = temp;
    }

    return rand() % (max_n - min_n + 1) + min_n;
}


/*
 * Function: executeit
 * 
 * Description:
 *
 * This function will start executing at the function address passed into it 
 * and return an integer return value that will be used to indicate pass(0)/fail(1)
 *
 * INPUTs:  funct_t start_addr :      function pointer 
 *
 * Returns:  int                :      0 for pass, 1 for fail
 */
int executeit(funct_t start_addr)
{
    volatile int i, rc = 0;
    i = 0;
    rc = (*start_addr)();
    return 0;
}

// Builds function prologue: sets up stack frame and saves callee-saved registers
static inline volatile char *enter( volatile char *target_address, int imm)
{
	
		*(int *)target_address = ((0) << 20) | ((imm) << ISZ_8) | 0xC8;
		 
		 target_address += BYTE4_OFF;
			
        return(target_address);
}
// Builds function epilogue: restores registers and cleans up stack frame

static inline volatile char *leave(volatile char *target_address)
{
	
	*(char *)target_address = 0xC9;
	target_address += BYTE1_OFF;
		 
	return(target_address);
}

// Appends return instruction to the instruction stream
static inline volatile char *ret(volatile char *target_address)
{

	*(char *)target_address = 0xC3;
		 
	target_address += BYTE1_OFF;

	return(target_address);
}

// Adds prologue and pushes callee-saved registers to stack
static inline volatile char *add_headeri( volatile char *target_address)
{

	target_address = enter(target_address, 2048);
	

    target_address=build_push_reg(REG_EBX,0,target_address);
    target_address=build_push_reg(REG_R12,1,target_address);
  	target_address=build_push_reg(REG_R13,1,target_address);
	target_address=build_push_reg(REG_R14,1,target_address);
	target_address=build_push_reg(REG_R15,1,target_address);
	
		return(target_address);
}

// Adds epilogue: pops registers, leaves stack frame, returns
static inline volatile char *add_endi( volatile char *target_address)
{	
	
	target_address=build_pop_reg(REG_R15,1,target_address);
 	target_address=build_pop_reg(REG_R14,1,target_address);
	target_address=build_pop_reg(REG_R13,1,target_address);
	target_address=build_pop_reg(REG_R12,1,target_address);	
    target_address=build_pop_reg(REG_EBX,0,target_address);
		
	target_address = leave(target_address);
	target_address = ret(target_address);
	
	return(target_address);
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
int build_instructions()
{
    srand(seed);

    int i, instruction_type;
    short mov_size;
    int src_reg, dest_reg, imm_value;
    long displacement;

    // Add prologue
    next_ptr = add_headeri(next_ptr);

    // Move memory pointer into EAX (setup)
    uintptr_t memory_location = (uintptr_t)mdptr;
    printf("mdptr = %p\n", mdptr);
    next_ptr = build_imm_to_register(ISZ_8, REG_EAX, memory_location, next_ptr);
    printf("Loading mdptr (address = %p) into RAX\n", (void *)memory_location);


    for (i = 0; i < num_instructions; i++)
    {
        instruction_type = rand_range(0, 3);  // 0: reg->reg, 1: imm->reg, 2: reg->mem, 3: mem->reg

        // Generate random size, with remapped values for valid ISZ_
        mov_size = rand_range(1, 8);
        if (mov_size == 3) mov_size = 4;
        if (mov_size == 5) mov_size = 8;
        if (mov_size == 6) mov_size = 1;
        if (mov_size == 7) mov_size = 2;

        src_reg = rand_range(1, 7);       // 1 to 7 = ECX to EDI
        if (instruction_type == 2 || instruction_type == 3) {
            dest_reg = REG_EAX;  // Force RAX for memory base
         } else {
              dest_reg = rand_range(1, 7);  // Random register for reg->reg or imm->reg
         }

 
        imm_value = rand_range(0, 100);   // Immediate value
        displacement = rand_range(0x10, 0x200);  // Offset for memory access


        printf("instruction_type = %d, mov_size = %d, src_reg = %d, dest_reg = %d, imm_value = %d\n",
               instruction_type, mov_size, src_reg, dest_reg, imm_value);

        switch (instruction_type)
        {
        case 0: // reg to reg
            next_ptr = build_mov_register_to_register(mov_size, src_reg, dest_reg, next_ptr);
            num_inst++;
            break;

        case 1: // imm to reg
            next_ptr = build_imm_to_register(mov_size, dest_reg, imm_value, next_ptr);
            num_inst++;
            break;

        case 2: // reg to mem 
            next_ptr = build_reg_to_memory(mov_size, src_reg, dest_reg, displacement, next_ptr);
            num_inst++;
            break;

        case 3: // mem to reg 
            next_ptr = build_memory_to_reg(mov_size, src_reg, dest_reg, displacement, next_ptr);
            num_inst++;
            break;
        }
    }

    printf("For loop done\n");
    
    // Add function epilogue to instruction stream
    next_ptr = add_endi(next_ptr);  // add return instruction
    printf("add_endi done\n");

    return num_inst;
}