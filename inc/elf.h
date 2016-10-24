#ifndef JOS_INC_ELF_H
#define JOS_INC_ELF_H

#define ELF_MAGIC 0x464C457FU	/* "\x7FELF" in little endian */

// See https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
// for more on the ELF binary format
// This should be named `Elfhdr`, as it describes the ELF header format
struct Elf {
	uint32_t e_magic;	     // Must equal ELF_MAGIC
	uint8_t e_elf[12];
	uint16_t e_type;       // Relocatable, executable, shared, or core
	uint16_t e_machine;    // Target ISA
	uint32_t e_version;    // 1 for orig version of ELF
	uint32_t e_entry;      // Address of entry point where program starts executing
	uint32_t e_phoff;      // Start of program header table
	uint32_t e_shoff;      // Start of section header table
	uint32_t e_flags;
	uint16_t e_ehsize;     // Size of this header--64/52 bytes for 64/32 bit archs
	uint16_t e_phentsize;  // Size of a program header table entry
	uint16_t e_phnum;			 // Number of entries in the program header table
	uint16_t e_shentsize;  // Size of a section header table entry
	uint16_t e_shnum;			 // Number of entries in the section header table
	uint16_t e_shstrndx;
};

// Describes how to create a process image.
// Found at file offset `e_phoff` and has `e_phnum` entries of `e_phentsize` bytes
struct Proghdr {
	uint32_t p_type;
	uint32_t p_offset;  // Segment's offset in the binary
	uint32_t p_va;      // Segment's virtual address
	uint32_t p_pa;			// Segment's physical address, if relevant
	uint32_t p_filesz;  // Segment's size (bytes) in binary. May be 0.
	uint32_t p_memsz;   // Segment's desired final size in RAM (>= filesz). May be 0.
	uint32_t p_flags;
	uint32_t p_align;
};

struct Secthdr {
	uint32_t sh_name;  // Offset to a string in the .shstrtab section that represents this section's name
	uint32_t sh_type;
	uint32_t sh_flags;
	uint32_t sh_addr;    // Virtual address of this section in memory, for loaded sections
	uint32_t sh_offset;  // Offset of section in the file image
	uint32_t sh_size;    // Size in bytes of section in the file image
	uint32_t sh_link;
	uint32_t sh_info;
	uint32_t sh_addralign;
	uint32_t sh_entsize;
};

// Values for Proghdr::p_type
#define ELF_PROG_LOAD		1

// Flag bits for Proghdr::p_flags
#define ELF_PROG_FLAG_EXEC	1
#define ELF_PROG_FLAG_WRITE	2
#define ELF_PROG_FLAG_READ	4

// Values for Secthdr::sh_type
#define ELF_SHT_NULL		0
#define ELF_SHT_PROGBITS	1
#define ELF_SHT_SYMTAB		2
#define ELF_SHT_STRTAB		3

// Values for Secthdr::sh_name
#define ELF_SHN_UNDEF		0

#endif /* !JOS_INC_ELF_H */
