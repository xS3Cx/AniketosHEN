/* The keyboard & mouse agent the daemon injects into games:
   extern const uint8_t kbm_agent_elf_start[]; extern const int kbm_agent_elf_size; */
#ifndef ONIONHEN_KBM_AGENT_ELF
#define ONIONHEN_KBM_AGENT_ELF "../../bin/kbm_agent.elf"
#endif

__asm__(
	".pushsection .rodata\n"
	".global kbm_agent_elf_start\n"
	".type   kbm_agent_elf_start, @object\n"
	".align  16\n"
	"kbm_agent_elf_start:\n"
		".incbin \"" ONIONHEN_KBM_AGENT_ELF "\"\n"
	"kbm_agent_elf_end:\n"
	".global kbm_agent_elf_size\n"
	".type   kbm_agent_elf_size, @object\n"
	".align  4\n"
	"kbm_agent_elf_size:\n"
		".int    kbm_agent_elf_end - kbm_agent_elf_start\n"
	".popsection\n"
);
