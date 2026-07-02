
//
// simple encoding examples for IA-32 validtion project
//

#include <stdio.h>
#include <stdlib.h>
#include "ia32_encode.h"

// globals to aid debug to start
volatile char *mptr=0,*next_ptr=0;
int num_inst=0;
int build_instructions();

int main(int argc, char *argv[])
{

	int ibuilt=0;


	/* allocate buffer to build instructions into */

	mptr=malloc(MAX_INSTR_BYTES);
	next_ptr=mptr;                  // init next_ptr

	ibuilt=build_instructions();  	// build instructions

	fprintf(stderr,"generation program complete, instructions generated: %d\n",ibuilt);
}

//
// Routine:  build_instructions
//
// Description:
//
// INPUT: none yet
// 
// OUTPUT: return the number of instructions built
// 
int build_instructions() {
	
	 // mov ecx into ebx (register to register)
    next_ptr = build_mov_register_to_register(ISZ_4, REG_ECX, REG_EBX, next_ptr);
    num_inst++;
    fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);

    // mov edx into edi (register to register)
    next_ptr = build_mov_register_to_register(ISZ_4, REG_EDX, REG_EDI, next_ptr);
    num_inst++;
    fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);

    // mov eax into esi (register to register)
    next_ptr = build_mov_register_to_register(ISZ_4, REG_EAX, REG_ESI, next_ptr);
    num_inst++;
    fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);
	
	

    // mov eax, 0x12345678 (32-bit immediate to eax)
    next_ptr = build_imm_to_register(ISZ_4, 0x12345678, REG_EAX, next_ptr);
    num_inst++;
    fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);

    // mov edx, 0x9abcdef0 (32-bit immediate to edx)
    next_ptr = build_imm_to_register(ISZ_4, 0x9abcdef0, REG_EDX, next_ptr);
    num_inst++;
    fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);

    // mov ecx, 0xdeadbeef (32-bit immediate to ecx)
    next_ptr = build_imm_to_register(ISZ_4, 0xdeadbeef, REG_ECX, next_ptr);
    num_inst++;
    fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);

   

    // mov edx to [ecx + 8] (displacement 8)
	next_ptr = build_reg_to_memory(ISZ_4, REG_EDX, REG_ECX, 2, 8, next_ptr);
	num_inst++;
	fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);

	// mov ebp to [eax + 10] (displacement 10)
	next_ptr = build_reg_to_memory(ISZ_4, REG_EBP, REG_EAX, 1, 10, next_ptr);
	num_inst++;
	fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);

	// mov ebx to [esi + 12] (displacement 12)
	next_ptr = build_reg_to_memory(ISZ_4, REG_EBX, REG_ESI, 7, 12, next_ptr);
	num_inst++;
	fprintf(stderr, "next ptr is now 0x%lx\n", (long)next_ptr);
	
	
	
	*next_ptr++ = 0xC3; // x86 opcode for RET
    num_inst++;

    return num_inst;
}
