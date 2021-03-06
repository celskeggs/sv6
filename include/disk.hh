#pragma once

// IDE supports at most a 64K DMA request
#define DISK_REQMAX     65536

struct kiovec
{
  void *iov_base;
  u64 iov_len;
};

class disk
{
public:
  disk() {}
  disk(const disk &) = delete;
  disk &operator=(const disk &) = delete;

  uint64_t dk_nbytes;
  char dk_model[40];
  char dk_serial[20];
  char dk_firmware[8];
  char dk_busloc[20];
  bool can_have_partitions = true;
  u32 devno = 0xFFFFFFFF;

  virtual void readv(kiovec *iov, int iov_cnt, u64 off) = 0;
  virtual void writev(kiovec *iov, int iov_cnt, u64 off) = 0;
  virtual void flush() = 0;

  void read(char* buf, u64 nbytes, u64 off) {
    kiovec iov = { (void*) buf, nbytes };
    readv(&iov, 1, off);
  }

  void write(const char* buf, u64 nbytes, u64 off) {
    kiovec iov = { (void*) buf, nbytes };
    writev(&iov, 1, off);
  }
};

typedef void (*disk_listener)(disk *d);

void disk_register(disk *d);
u32 disk_find_root();
disk *disk_find(const char *description);
disk *disk_by_devno(u32 devno);
void disk_read(u32 dev, char* data, u64 count, u64 offset);
void disk_write(u32 dev, const char* data, u64 count, u64 offset);

void disk_subscribe(disk_listener l);
