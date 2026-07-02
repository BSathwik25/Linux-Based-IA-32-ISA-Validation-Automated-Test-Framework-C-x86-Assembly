/*
 * Description:
 *
 *
 * References: Intel 64 and IA-32 Architecture Software Developers Manual (SDM)
 *
 * GENERAL Instruction Format
 *
 * -----------------------------------------------------------------
 * | Instruciton    |   Opcode | ModR/M | Displacement | Immediate |
 * | Prefixes       |          |        |              |           |
 * -----------------------------------------------------------------
 *
 *  7  6  5   3  2   0
 * --------------------
 * | Mod | Reg* | R/M |
 * --------------------
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <limits.h>    /* for PAGESIZE */

/*
 * definitions we need to support these functions.
 *
 * see Table 2-2 in SDM for register/MODRM encode usage
 *
 *
 */
#define PREFIX_16BIT   0x66
#define PREFIX_32BIT   0x67
#define BASE_MODRM     0xc0
#define BASE_MODRM4    0x00
#define BASE_MODRM5    0x40
#define BASE_MODRM6    0x80

#define REG_SHIFT      0x3
#define MODRM_SHIFT    0x6
#define RM_SHIFT       0x0
#define REG_MASK       0x7
#define RM_MASK        0x7
#define MOD_MASK       0x3

// register defs based on MOD RM table
#define REG_EAX        0x0
#define REG_ECX        0x1
#define REG_EDX        0x2
#define REG_EBX        0x3
#define REG_ESP        0x4
#define REG_EBP        0x5
#define REG_ESI        0x6
#define REG_EDI        0x7

// register defs based for x86_64, requires REX extension
#define MYREG_R8        0x0
#define MYREG_R9        0x1
#define MYREG_R10       0x2
#define MYREG_R11       0x3
#define MYREG_R12       0x4
#define MYREG_R13       0x5
#define MYREG_R14       0x6
#define MYREG_R15       0x7

// byte offset
#define BYTE1_OFF      0x1
#define BYTE2_OFF      0x2
#define BYTE3_OFF      0x3
#define BYTE4_OFF      0x4
#define BYTE6_OFF      0X6

// ISIZE (instruction size in bytes, for move example 2byte = 16bit)

#define ISZ_1         0x1
#define ISZ_2         0x2
#define ISZ_4         0x4
#define ISZ_8         0x8

//Prefix for 64 bit 
#define REX_PREFIX  0x40
#define REX_W         0x8
#define REX_R         0x4
#define REX_X         0x2
#define REX_B         0x1

// code generation defines

#define MAX_THREADS     5
#define MAX_DEF_INSTRS  10
#define MAX_INSTR_BYTES (3*PAGESIZE)   // allocate 3  PAGES for instruction
#define MAX_DATA_BYTES  (10*PAGESIZE)  // allocate 10 PAGES for data
#define MAX_COMM_BYTES  (PAGESIZE)     // allocate 1  PAGE for communications

// information sharing between tasks
#define NUM_PTRS 3
#define CODE 0
#define DATA 1
#define COMM 2
#define MAX_INSTR_BYTES 10000
#define MAX_DATA_BYTE  (10*1024)  // allocate 10K

/*
 * Function: build_mov_register_to_register
 *
 * Description:
 *
 * Inputs: 
 *
 *  short mov_size               :  size of the move being requested
 *  int   src_reg                :  register source encoding 
 *  int   dest_reg               :  destintion reg of move
 *  volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *
 *  returns adjusted address after encoding instruction
 *
 */


static inline volatile char *build_mov_register_to_register(short mov_size, int src_reg, int dest_reg, volatile char *tgt_addr)
{
	// for 16 bit mode we need to treat it special because it requires a prefix
	if (mov_size == 8) {
	   (*(char*)tgt_addr)= (REX_PREFIX|REX_W);
	    tgt_addr+=BYTE1_OFF;
	}


	if (mov_size == 2) {
		(*tgt_addr ++) = PREFIX_16BIT;
	}

        
	// now lets look at each size and determine which opcode required

	switch(mov_size)  {

	case 1: 
		(*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8a;
		 tgt_addr += BYTE2_OFF;
		 break;

	case 2:  // can overload this case because same opcode, but already set prefix
	case 4: 
		(*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8b;
		 tgt_addr += BYTE2_OFF;
		 break;
        case 8: (*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8b;
		 tgt_addr += BYTE2_OFF;
		 break;

	default:
		 fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to register move\n", mov_size);
		 return (NULL);

	}
		
        return(tgt_addr);
}


/*
 * Function: build_push_reg
 *
 * Description:
 *
 * builds push register
 *
 * Inputs: 
 *
 *  int reg_index                :  index of register
 *  int x86_64f                  :  flag to indicate if we need to extend to rex format
 *  volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *
 *  returns adjusted address after encoding instruction
 *
 */


static inline volatile char *build_push_reg(int reg_index, int x86_64f, volatile char *tgt_addr)
{

	if (x86_64f) {
		// this is a quick hack for REX_B prefix

		(*(char *) tgt_addr)=(REX_PREFIX | REX_B);
		tgt_addr += BYTE1_OFF;
	}

	(*(char *) tgt_addr) = 0x50+reg_index;;
	tgt_addr += BYTE1_OFF;
			
        return(tgt_addr);
}

/*
 * Function: build_pop_reg
 *
 * Description:
 *
 * builds pop register
 *
 * Inputs: 
 *
 *  int reg_index                :  index of register
 *  int x86_64f                  :  flag to indicate if we need to extend to rex format
 *  volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *
 *  returns adjusted address after encoding instruction
 *
 */


static inline volatile char *build_pop_reg(int reg_index, int x86_64f, volatile char *tgt_addr)
{

	if (x86_64f) {
		// this is a quick hack for REX_B prefix

		(*(char *) tgt_addr)=(REX_PREFIX | REX_B);
		tgt_addr += BYTE1_OFF;
	}

	(*(char *) tgt_addr) = 0x58+reg_index;;
	tgt_addr += BYTE1_OFF;
			
        return(tgt_addr);
}


static inline volatile char *build_pusha(volatile char *tgt_addr)
{
	(*(char *) tgt_addr) = 0x60;
	tgt_addr += BYTE1_OFF;
			
        return(tgt_addr);
}


//function for immediate to register mov instruction 

static inline volatile char *build_imm_to_register(short mov_size, long imm_value, int dest_reg, volatile char *tgt_addr)
{
	// for 16 bit mode we need to treat it special because it requires a prefix
//// For 64 bit ///////// prefix and rex.w is selected 
	if (mov_size == 8) {
	   (*(char*)tgt_addr)= (REX_PREFIX|REX_W);
	    tgt_addr+=BYTE1_OFF;
	}
//////for 16 bit prefix/// is selected 
        if (mov_size == 2) {
		(*tgt_addr ++) = PREFIX_16BIT;
	}

        

	// now lets look at each size and determine which opcode required

	switch(mov_size)  {

	case 1: 
		(*(long *) tgt_addr) = ((imm_value) << 16) | (((BASE_MODRM) + dest_reg) << 8) | 0xc6;
		 tgt_addr += BYTE3_OFF;
		 break;

	case 2:
		(*(long *) tgt_addr) = ((imm_value) << 16) | (((BASE_MODRM) + dest_reg) << 8) | 0xc7;
		 tgt_addr += BYTE4_OFF;
		 break;
		 
	case 4: 
		(*(long *) tgt_addr) = ((imm_value) << 16) | (((BASE_MODRM) + dest_reg) << 8) | 0xc7;
		 tgt_addr += BYTE6_OFF;
		 break;
        case 8: 
		 (*(char*) tgt_addr) = (0xb8+dest_reg);
		 tgt_addr+=BYTE1_OFF;
		 (*(long *)tgt_addr)=imm_value;
		 tgt_addr+=8;
		 break;


	default:
		 fprintf(stderr,"ERROR: Incorrect size (%d) passed to immediate to register move\n", mov_size);
		 return (NULL);

	}
			
        return(tgt_addr);
}

// function for register to memory mov instruction
static inline volatile char *build_reg_to_memory(short mov_size, int src_reg, int dest_reg,long displacement, volatile char *tgt_addr)
{
/// for 64 bit prefix and rex.w
        if (mov_size == 8) {
	   (*(char*)tgt_addr)= (REX_PREFIX|REX_W);
	    tgt_addr+=BYTE1_OFF;
	}
// 

	if (mov_size == 2) {
		(*tgt_addr ++) = PREFIX_16BIT;
	}

        
        
	// now lets look at each size and determine which opcode required
        
	if(displacement==0)
	{

	switch(mov_size)  {

	case 1: 
                (*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x88;
		 tgt_addr += BYTE2_OFF;
		 break;

	case 2:  // can overload this case because same opcode, but already set prefix
	case 4: 
                (*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;

		 tgt_addr += BYTE2_OFF;
		 break;
   
        case 8: 
		(*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
	  		
		  tgt_addr += BYTE2_OFF;
		  break;

	default:
		 fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to memory move\n", mov_size);
		 return (NULL);

	}
    }
	else if (displacement>=0x1 && displacement<=0x7F)
	
	{
		
		switch(mov_size)  {

			case 1: 
			(*(long *) tgt_addr) =( (displacement)<<16 | ((BASE_MODRM5) + (src_reg << REG_SHIFT) + dest_reg )<< 8 )| 0x88;
			 
			 tgt_addr += BYTE3_OFF;
			 break;

			case 2:  // can overload this case because same opcode, but already set prefix
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM5) + (src_reg << REG_SHIFT) + dest_reg) << 8 )| 0x89;
	  		
			 tgt_addr += BYTE3_OFF;
			 break;

			case 4: 
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM5) + (src_reg << REG_SHIFT) + dest_reg) << 8 )| 0x89;
	  		
			 tgt_addr += BYTE3_OFF;
			 break;

			case 8: 
			(*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
	  		
			 tgt_addr += BYTE2_OFF;
			 break;

			default:
			 fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to memory displacement move\n", mov_size);
			 return (NULL);

		}
	}



	else

	{

		
		switch(mov_size)  {

			case 1: 
			(*(long *) tgt_addr) =( (displacement)<<16 | ((BASE_MODRM6) + (src_reg << REG_SHIFT) + dest_reg )<< 8 )| 0x88;
			 
			 tgt_addr += BYTE6_OFF;
			 break;

			case 2:  
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM6) + (src_reg << REG_SHIFT) + dest_reg) << 8 )| 0x89;
	  		
			 
			 tgt_addr += BYTE6_OFF;
			 break;

			case 4: 
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM6) + (src_reg << REG_SHIFT) + dest_reg) << 8 )| 0x89;
			 tgt_addr += BYTE6_OFF;
			 break;

			
			case 8: 
			(*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
	  		
			 tgt_addr += BYTE2_OFF;
			 break;

			default:
			 fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to memory displacement move\n", mov_size);
			 return (NULL);

		}






	}		
        return(tgt_addr);
}



// mov memory to register 
static inline volatile char *build_mov_memory_to_reg(short mov_size, int dest_reg, int src_reg,long displacement, volatile char *tgt_addr)
{
        // for 64 bit prefix selectiopn 	
	if (mov_size == 8) {
	   (*(char*)tgt_addr)= (REX_PREFIX|REX_W);
	    tgt_addr+=BYTE1_OFF;
	}


	if (mov_size == 2) {
		(*tgt_addr ++) = PREFIX_16BIT;
	}

	if(displacement==0)
	{

		switch(mov_size)  {

		case 1: 
			(*(short *) tgt_addr) = ((BASE_MODRM4) + (dest_reg << REG_SHIFT) + src_reg )<< 8 | 0x8A;
			 
			 tgt_addr += BYTE2_OFF;
			 break;

		case 2:  // can overload this case because same opcode, but already set prefix
		case 4: 
			(*(short *) tgt_addr) = ((BASE_MODRM4) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8B;
	  		
			 tgt_addr += BYTE2_OFF;

			 break;

		
		case 8: 
			(*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
	  		
			 tgt_addr += BYTE2_OFF;
			 break;
		default:
			 fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to register move\n", mov_size);
			 return (NULL);


		}
	}
	
	else if (displacement>=0x1 && displacement<=0x7F)
	
	{
		
		switch(mov_size)  {


			case 1: 
			(*(long *) tgt_addr) =( (displacement)<<16 | ((BASE_MODRM5) + (dest_reg << REG_SHIFT) + src_reg )<< 8 )| 0x8A;
			 
			 tgt_addr += BYTE3_OFF;

			 break;

			case 2:  // can overload this case because same opcode, but already set prefix
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM5) + (dest_reg << REG_SHIFT) + src_reg) << 8 )| 0x8B;
	  		
			 tgt_addr += BYTE3_OFF;
			 break;

			case 4: 
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM5) + (dest_reg << REG_SHIFT) + src_reg) << 8 )| 0x8B;
	  		
			 tgt_addr += BYTE3_OFF;
			 break;

			
			case 8: 
			(*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
	  		
			 tgt_addr += BYTE2_OFF;
			 break;

			default:
			 fprintf(stderr,"ERROR: Incorrect size (%d) passed to memory to register displacement move\n", mov_size);
			 return (NULL);

		}
	}



	else

	{

		
		switch(mov_size)  {

			case 1: 
			(*(long *) tgt_addr) =( (displacement)<<16 | ((BASE_MODRM6) + (dest_reg << REG_SHIFT) + src_reg )<< 8 )| 0x8A;
			 
			 tgt_addr += BYTE6_OFF;
			 break;

			case 2:  // can overload this case because same opcode, but already set prefix
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM6) + (dest_reg << REG_SHIFT) + src_reg) << 8 )| 0x8B;
	  		
			 
			 tgt_addr += BYTE6_OFF;
			 break;

			case 4: 
			(*(long *) tgt_addr) = ( (displacement)<<16) |(((BASE_MODRM6) + (dest_reg << REG_SHIFT) + src_reg) << 8 )| 0x8B;
			 tgt_addr += BYTE6_OFF;
			 break;

			case 8: 
			(*(short *) tgt_addr) = ((BASE_MODRM4) + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
	  		
			 tgt_addr += BYTE2_OFF;
			 break;

			default:
			 fprintf(stderr,"ERROR: Incorrect size (%d) memory to register to register move\n", mov_size);
			 return (NULL);

		}
    }		
        return(tgt_addr);
}

static inline volatile char *build_xadd(short mov_size, int src_reg, int dest_reg, volatile char *tgt_addr)
{
	// for 16 bit mode we need to treat it special because it requires a prefix

	if (mov_size == 2) {
		(*tgt_addr ++) = PREFIX_16BIT;
	}
        if (mov_size == 3) {
		(*tgt_addr ++) = PREFIX_32BIT;
	}
       


	// now lets look at each size and determine which opcode required

	switch(mov_size)  {

	case 1: 
		(*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0xc00f;
		 tgt_addr += BYTE2_OFF;
		 break;

	case 2:  // can overload this case because same opcode, but already set prefix
	case 4: 
		(*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0xc10f;
		 tgt_addr += BYTE2_OFF;
		 break;


	default:
		 fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to register move\n", mov_size);
		 return (NULL);

	}
			
        return(tgt_addr);
}


static inline volatile char *build_xchng(short mov_size, int src_reg, int dest_reg, volatile char *tgt_addr)
{
	// for 16 bit mode we need to treat it special because it requires a prefix

	if (mov_size == 2) {
		(*tgt_addr ++) = PREFIX_16BIT;
	}
	if (mov_size == 3) {
		(*tgt_addr ++) = PREFIX_32BIT;
	}


	// now lets look at each size and determine which opcode required

	switch(mov_size)  {

	case 1: 
		(*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x86;
		 tgt_addr += BYTE2_OFF;
		 break;

	case 2:  // can overload this case because same opcode, but already set prefix
	case 4: 
		(*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x87;
		 tgt_addr += BYTE2_OFF;
		 break;


	default:
		 fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to register move\n", mov_size);
		 return (NULL);

	}
			
        return(tgt_addr);
}
static inline volatile char *build_mfence(volatile char *tgt_addr)
{
	(*(short *) tgt_addr) = 0xae0f;
	tgt_addr += BYTE2_OFF;
	(*tgt_addr++)=0xf0;
    return(tgt_addr);
}



static inline volatile char *build_sfence(volatile char *tgt_addr)
{
	(*(short *) tgt_addr) = 0xae0f;
	tgt_addr += BYTE2_OFF;
	(*tgt_addr++)=0xf8;
    return(tgt_addr);
}


static inline volatile char *build_lfence(volatile char *tgt_addr)
{
	(*(short *) tgt_addr) = 0xae0f;
	tgt_addr += BYTE2_OFF;
	(*tgt_addr++)=0xe8;
    return(tgt_addr);
}

