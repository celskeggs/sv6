#include "types.h"
#include "kernel.hh"
#include "disk.hh"
#include "vector.hh"
#include "cmdline.hh"
#include <cstring>
#include "disk.hh"

static static_vector<disk*, 64> disks;
static static_vector<disk_listener, 64> listeners;

void
disk_register(disk *d)
{
  for (disk *existing : disks) {
    if (strcmp(d->dk_busloc, existing->dk_busloc) == 0) {
      panic("attempt to register a second disk with the bus location \"%s\"\n", d->dk_busloc);
    }
  }
  cprintf("disk_register(%lu): %s: %ld bytes: %s\n",
          disks.size(), d->dk_busloc, d->dk_nbytes, d->dk_model);
  disks.push_back(d);
  // note: disk listeners MAY call disk_register again!
  // FIXME: make sure this recursion can't break anything
  for (disk_listener l : listeners) {
    l(d);
  }
}

void
disk_subscribe(disk_listener l)
{
  listeners.push_back(l);
  for (disk *d : disks) {
    l(d);
  }
}

// FIXME: make this an automated test
static void
disk_test(disk *d)
{
  char buf[512];

  cprintf("testing disk %s\n", d->dk_busloc);

  cprintf("writing..\n");
  memset(buf, 0xab, 512);
  d->write(buf, 512, 0);

  cprintf("reading..\n");
  memset(buf, 0, 512);
  d->read(buf, 512, 0x2000);

  for (int i = 0; i < 512; i++)
    cprintf("%02x ", ((unsigned char*) buf)[i]);
  cprintf("\n");

  cprintf("flushing..\n");
  d->flush();

  cprintf("disk_test: test done\n");
}

static void
disk_test_all()
{
  for (disk* d : disks) {
    disk_test(d);
  }
}

//SYSCALL
void
sys_disktest(void)
{
  disk_test_all();
}

// for use in parsing the root_disk cmdline; can either take a number or a bus location
u32
disk_find(const char *description)
{
  // first, try parsing the description as a number
  char *end = nullptr;
  long ret;
  ret = strtol(description, &end, 10);
  if (*end == '\0') {
    if (ret < 0 || ret >= disks.size()) {
      panic("disk number %ld not identified (%lu disks were identified)", ret, disks.size());
    }
    return ret;
  }

  // if it's not a number, try it as a bus location
  u32 index = 0;
  for (disk* d : disks) {
    if (strcmp(description, d->dk_busloc) == 0) {
      return index;
    }
    index++;
  }

  panic("cannot identify disk with bus location \"%s\"", description);
}

u32
disk_find_root()
{
  return disk_find(cmdline_params.root_disk);
}

// compat for a single IDE disk..
void
disk_read(u32 dev, char* data, u64 count, u64 offset)
{
  if (dev >= disks.size())
    panic("disk %u not found (%lu disks were found)", dev, disks.size());
  while (count > DISK_REQMAX) {
    disks[dev]->read(data, DISK_REQMAX, offset);
    data += DISK_REQMAX;
    offset += DISK_REQMAX;
    count -= DISK_REQMAX;
  }
  disks[dev]->read(data, count, offset);
}

void
disk_write(u32 dev, const char* data, u64 count, u64 offset)
{
  if (dev >= disks.size())
    panic("disk %u not found (%lu disks were found)", dev, disks.size());
  while (count > DISK_REQMAX) {
    disks[dev]->write(data, DISK_REQMAX, offset);
    data += DISK_REQMAX;
    offset += DISK_REQMAX;
    count -= DISK_REQMAX;
  }
  disks[dev]->write(data, count, offset);
}
