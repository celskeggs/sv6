struct multiboot_info
{
  u32 flags;
  u32 mem_lower;        // flag 0
  u32 mem_upper;        // flag 0
  u32 boot_device;      // flag 1
  u32 cmdline;          // flag 2
  u32 mods_count;       // flag 3
  u32 mods_addr;
  u32 syms[4];          // flag 4, 5
  u32 mmap_length;      // flag 6
  u32 mmap_addr;
  u32 drives_length;    // flag 7
  u32 drives_addr;
  u32 config_table;     // flag 8
  u32 boot_loader_name; // flag 9
  u32 apm_table;        // flag 10
  u32 vbe_control_info; // flag 11
  u32 vbe_mode_info;
  u32 vbe_mode;
  u32 vbe_interface_seg;
  u32 vbe_interface_off;
  u32 vbe_interface_len;
};

struct multiboot_mem
{
  u32 size;
  u64 base;
  u64 length;
  u32 type;
} __attribute__((packed));

struct multiboot_mod
{
  u32 start;
  u32 end;
  u32 name;
};

struct multiboot_saved
{
  u32 flags = 0;
  u32 mem_lower;        // flag 0
  u32 mem_upper;        // flag 0
  u32 boot_device;      // flag 1

  // flag 2
  char cmdline[512] = { 0 };

  // flag 6
  u32 mmap_entries = 0;
  multiboot_mem mmap[32];

  // flag 9
  char boot_loader_name[128] = { 0 };
};
extern multiboot_saved multiboot;

#define MULTIBOOT_FLAG_MEM               (1 << 0)
#define MULTIBOOT_FLAG_BOOT_DEV          (1 << 1)
#define MULTIBOOT_FLAG_CMDLINE           (1 << 2)
#define MULTIBOOT_FLAG_MMAP              (1 << 6)
#define MULTIBOOT_FLAG_BOOT_LOADER_NAME  (1 << 9)


