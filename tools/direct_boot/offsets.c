#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define XNU_CMDLINE_LEN 608
struct xnu_boot_arguments {
	u16 revision;
	u16 version;
	u64 virt_base;
	u64 phys_base;
	u64 mem_size;
	u64 phys_end;
	struct xnu_video_information {
		u64 base_addr;
		u64 display;
		u64 bytes_per_row;
		u64 width;
		u64 height;
		u64 depth;
	} video_information;
	u32 machine_type;
	uintptr_t afdt;
	u32 afdt_length;
	char command_line[XNU_CMDLINE_LEN];
	u64 boot_flags;
	u64 mem_size_actual;
};
int main(void) {
    printf("sizeof=%zu\n", sizeof(struct xnu_boot_arguments));
    printf("off_virt_base=%zu\n", offsetof(struct xnu_boot_arguments, virt_base));
    printf("off_phys_base=%zu\n", offsetof(struct xnu_boot_arguments, phys_base));
    printf("off_mem_size=%zu\n", offsetof(struct xnu_boot_arguments, mem_size));
    printf("off_phys_end=%zu\n", offsetof(struct xnu_boot_arguments, phys_end));
    printf("off_video=%zu\n", offsetof(struct xnu_boot_arguments, video_information));
    printf("off_machine_type=%zu\n", offsetof(struct xnu_boot_arguments, machine_type));
    printf("off_afdt=%zu\n", offsetof(struct xnu_boot_arguments, afdt));
    printf("off_afdt_length=%zu\n", offsetof(struct xnu_boot_arguments, afdt_length));
    printf("off_command_line=%zu\n", offsetof(struct xnu_boot_arguments, command_line));
    printf("off_boot_flags=%zu\n", offsetof(struct xnu_boot_arguments, boot_flags));
    printf("off_mem_size_actual=%zu\n", offsetof(struct xnu_boot_arguments, mem_size_actual));
    return 0;
}
