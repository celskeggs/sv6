#include "types.h"
#include "kernel.hh"
#include "mmu.h"
#include "amd64.h"
#include "spinlock.hh"
#include "condvar.hh"
#include "proc.hh"
#include "cpu.hh"
#include "bits.hh"
#include "kmtrace.hh"
#include "kalloc.hh"
#include "vm.hh"
#include "ns.hh"
#include "filetable.hh"
#include <uk/fcntl.h>
#include <uk/unistd.h>
#include <uk/wait.h>

u64
proc::hash(const u32 &p)
{
  return p;
}

xns<u32, proc*, proc::hash> *xnspid __mpalign__;
struct proc *bootproc __mpalign__;

#if MTRACE
struct kstack_tag kstack_tag[NCPU];
#endif

enum { sched_debug = 0 };

proc::proc(int npid) :
  kstack(0), qstack(0), killed(0), tf(0), uaccess_(0), user_fs_(0), pid(npid),
  unmapped_hint(0), cv(nullptr), yield_(false), oncv(0), cv_wakeup(0), curcycles(0),
  tsc(0), cpuid(0), cpu_pin(0), context(nullptr), on_qstack(false), state_(EMBRYO),
  parent(0), fpu_state(nullptr), unmap_tlbreq_(0), data_cpuid(-1), in_exec_(0),
  upath(nullptr), uargv(nullptr), exception_inuse(0), magic(PROC_MAGIC)
{
  snprintf(lockname, sizeof(lockname), "cv:proc:%d", pid);
  lock = spinlock(lockname+3, LOCKSTAT_PROC);

  gc = new gc_handle();
  memset(__cxa_eh_global, 0, sizeof(__cxa_eh_global));
  memset(sig, 0, sizeof(sig));
}

proc::~proc(void)
{
  magic = 0;
  if (fpu_state)
    kmfree(fpu_state, FXSAVE_BYTES);
  fpu_state = nullptr;
}

void
proc::set_state(enum procstate s)
{
  switch(state_) {
  case EMBRYO:
    if (s != RUNNABLE)
      panic("EMBRYO -> %u", s);
    break;
  case SLEEPING:
    if (s != RUNNABLE)
      panic("SLEEPING -> %u", s);
    break;
  case RUNNABLE:
    if (s != RUNNING && s != RUNNABLE)
      panic("RUNNABLE -> %u", s);
    break;
  case RUNNING:
    if (s != RUNNABLE && s != SLEEPING && s != ZOMBIE)
      panic("RUNNING -> %u", s);
    break;
  case ZOMBIE:
    panic("ZOMBIE -> %u", s);
  }
  state_ = s;
}

int
proc::set_cpu_pin(int cpu)
{
  if (cpu < -1 || cpu >= ncpu)
    return -1;

  acquire(&lock);
  if (myproc() != this)
    panic("set_cpu_pin not implemented for non-current proc");
  if (cpu == -1) {
    cpu_pin = 0;
    release(&lock);
    return 0;
  }
  // Since we're the current proc, there's no runq to get off.
  // post_swtch will put us on the new runq.
  cpuid = cpu;
  cpu_pin = 1;
  myproc()->set_state(RUNNABLE);
  sched(true);
  assert(mycpu()->id == cpu);
  return 0;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&myproc()->lock);  //DOC: yieldlock
  myproc()->set_state(RUNNABLE);
  myproc()->yield_ = false;
  sched(true);
}


// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
u64
forkret(void)
{
  post_swtch();

  // Just for the first process. can't do it earlier
  // b/c file system code needs a process context
  // in which to call condvar::sleep().
  if(myproc()->cwd == nullptr) {
    mtstart(forkret, myproc());
    myproc()->cwd = vfs_root()->root();
    mtstop(myproc());
  }

  // Return to "caller", actually trapret (see allocproc).
  return myproc()->user_fs_;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(int status)
{
  int wakeupinit;

  if(myproc() == bootproc)
    panic("init exiting");

  myproc()->ftable.reset();

  myproc()->cwd.reset();

  myproc()->status = (status & __WAIT_STATUS_VAL_MASK) | __WAIT_STATUS_EXITED;

  // Pass abandoned children to init.
  wakeupinit = 0;
  while (!myproc()->childq.empty()) {
    auto &p = myproc()->childq.front();
    myproc()->childq.pop_front();
    scoped_acquire pl(&p.lock);
    scoped_acquire bl(&bootproc->lock);
    p.parent = bootproc;
    if(p.get_state() == ZOMBIE)  {
      wakeupinit = 1;
    }
    bootproc->childq.push_back(&p);
  }

  // Release vmap
  if (myproc()->vmap != nullptr) {
    sref<vmap> vmap = std::move(myproc()->vmap);
    // Switch to kernel page table, since we may be just about to
    // destroy the current page table.
    switchvm(vmap.get(), nullptr);

    // Remove user visible state associated with this proc from vmap.
    vmap->remove((uptr)myproc(), PGSIZE);
    vmap->remove((uptr)myproc()->kstack, KSTACKSIZE);

    if (myproc()->cv) {
      vmap->qfree(myproc()->cv);
      myproc()->cv = nullptr;
    }
  }

  // Lock the parent first, since otherwise we might deadlock.
  if (myproc()->parent != nullptr)
    acquire(&myproc()->parent->lock);

  acquire(&(myproc()->lock));

  // Kernel threads might not have a parent
  if (myproc()->parent != nullptr) {
    release(&myproc()->parent->lock);
    myproc()->parent->cv->wake_all();
  } else {
    idlezombie(myproc());
  }

  if (wakeupinit)
    bootproc->cv->wake_all();

  // Clean up FPU state
  if (myproc()->fpu_state) {
    // Make sure no CPUs think this process is the FPU owner
    for (int i = 0; i < ncpu; ++i) {
      struct proc *copy = myproc();
      atomic_compare_exchange_strong(&cpus[i].fpu_owner, &copy, (proc*)nullptr);
    }
  }

  // Jump into the scheduler, never to return.
  myproc()->set_state(ZOMBIE);
  sched(true);
  panic("zombie exit");
}

static void
freeproc(struct proc *p)
{
  delete p;
}

proc*
proc::alloc(void)
{
  char *sp;
  proc* p;

  p = new proc(xnspid->allockey());
  if (p == nullptr)
    throw_bad_alloc();

  p->cpuid = mycpu()->id;
#if MTRACE
  p->mtrace_stacks.curr = -1;
#endif

  if (!xnspid->insert(p->pid, p))
    panic("allocproc: ns_insert");

  // Allocate kernel stacks.
  try {
    if(!(p->qstack = (char*) kalloc("qstack", KSTACKSIZE)))
      throw_bad_alloc();

#if KSTACK_DEBUG && false // TODO: fix kstack debugging
    // vmalloc the stack to surround it with guard pages so we can
    // detect stack over/underflows.
    p->kstack_vm = vmalloc<char[]>(KSTACKSIZE);
    p->kstack = p->kstack_vm.get();
#else
    if(!(p->kstack = (char*) kalloc("kstack", KSTACKSIZE)))
      throw_bad_alloc();
#endif
  } catch (...) {
    if (!xnspid->remove(p->pid, &p))
      panic("allocproc: ns_remove");
    freeproc(p);
    throw;
  }

  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // amd64 ABI mandates sp % 16 == 0 before a call instruction
  // (or after executing a ret instruction)
  if ((uptr) sp % 16)
    panic("allocproc: misaligned sp");

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 8;
  *(u64*)sp = (u64)trapret;

  sp -= sizeof(struct context);
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof(struct context));
  p->context->rip = (uptr)forkret;

  return p;
}

void proc::init_vmap()
{
  vmap->qinsert(this);
  vmap->qinsert(kstack, qstack, KSTACKSIZE);

  // Ideally this would be part of the same allocation as the current proc.
  cv = (condvar*)vmap->qalloc("proc::cv");
  new (cv) condvar();
}

void
initproc(void)
{
  xnspid = new xns<u32, proc*, proc::hash>(false);
  if (xnspid == 0)
    panic("pinit");
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
proc::kill(void)
{
  acquire(&lock);
  killed = 1;
  if(get_state() == SLEEPING) {
    // we need to wake p up if it is condvar::sleep()ing.
    // can't change p from SLEEPING to RUNNABLE since that
    //   would make some condvar->waiters a dangling reference,
    //   and the non-zero p->cv_next will cause a future panic.
    // can't release p->lock then call wake_all() since the
    //   cv might be deallocated while we're using it
    //   (pipes dynamically allocate condvars).
    // can't call p->oncv.wake_all() since that results in
    //   deadlock (wake_all() acquires p->lock).
    // changed the wake_all API to avoid double locking of p.
    oncv->wake_all(0, this);
  }
  release(&lock);
  return 0;
}

int
proc::kill(int pid)
{
  struct proc *p;

  // XXX The one use of lookup and it is wrong: it should return a locked
  // proc structure, or be in an RCU epoch.  Now another process can delete
  // p between lookup and kill.
  p = xnspid->lookup(pid);
  if (p == 0) {
    panic("kill");
    return -1;
  }
  return p->kill();
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdumpall(void)
{
  static const char *states[] = {
    /* [EMBRYO]   = */ "embryo",
    /* [SLEEPING] = */ "sleep ",
    /* [RUNNABLE] = */ "runble",
    /* [RUNNING]  = */ "run   ",
    /* [ZOMBIE]   = */ "zombie"
  };
  const char *name = "(no name)";
  const char *state;
  uptr pc[10];

  for (proc *p : xnspid) {
    if(p->get_state() >= 0 && p->get_state() < NELEM(states) && 
       states[p->get_state()])
      state = states[p->get_state()];
    else
      state = "???";
    
    if (p->name && p->name[0] != 0)
      name = p->name;
    
    cprintf("\n%-3d %-10s %8s %2u  %lu\n",
            p->pid, name, state, p->cpuid, p->tsc);
    
    if(p->get_state() == SLEEPING){
      getcallerpcs((void*)p->context->rbp, pc, NELEM(pc));
      for(int i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %lx\n", pc[i]);
    }
  }
}

// Create a new process copying p as the parent.  Sets up stack to
// return as if from system call.  By default, the new process shares
// nothing with its parent and it is made RUNNABLE.
struct proc*
doclone(clone_flags flags)
{
  struct proc *np;

  // cprintf("%d: fork\n", myproc()->pid);

  // Allocate process.
  if((np = proc::alloc()) == 0)
    return nullptr;

  auto proc_cleanup = scoped_cleanup([&np]() {
    if (!xnspid->remove(np->pid, &np))
      panic("fork: ns_remove");
    freeproc(np);
  });

  if (flags & CLONE_SHARE_VMAP) {
    np->vmap = myproc()->vmap;
  } else if (!(flags & CLONE_NO_VMAP)) {
    // Copy process state from p.
    np->vmap = myproc()->vmap->copy();
  }
  np->init_vmap();

  np->parent = myproc();
  *np->tf = *myproc()->tf;
  np->cpu_pin = myproc()->cpu_pin;
  np->data_cpuid = myproc()->data_cpuid;
  np->run_cpuid_ = myproc()->run_cpuid_;
  np->user_fs_ = myproc()->user_fs_;
  memcpy(np->sig, myproc()->sig, sizeof(np->sig));

  // Clear %eax so that fork returns 0 in the child.
  np->tf->rax = 0;

  if (flags & CLONE_SHARE_FTABLE) {
    np->ftable = myproc()->ftable;
  } else if (!(flags & CLONE_NO_FTABLE)) {
    np->ftable = myproc()->ftable->copy();
  }

  np->cwd = myproc()->cwd;
  safestrcpy(np->name, myproc()->name, sizeof(myproc()->name));
  acquire(&myproc()->lock);
  myproc()->childq.push_back(np);
  release(&myproc()->lock);

  np->cpuid = mycpu()->id;
  if (!(flags & CLONE_NO_RUN)) {
    acquire(&np->lock);
    addrun(np);
    release(&np->lock);
  }

  proc_cleanup.dismiss();
  return np;
}

void
finishproc(struct proc *p)
{
  if (!xnspid->remove(p->pid, &p))
    panic("finishproc: ns_remove");
#if !KSTACK_DEBUG
  if (p->kstack)
    kfree(p->kstack, KSTACKSIZE);
  if (p->qstack)
    kfree(p->qstack, KSTACKSIZE);
#endif

  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  freeproc(p);
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(int wpid,  userptr<int> status)
{
  int havekids, pid;

  for(;;){
    // Scan children for ZOMBIEs
    havekids = 0;
    acquire(&myproc()->lock);
    for (auto it = myproc()->childq.begin(); it != myproc()->childq.end(); it++) {
      proc &p = *it;
      acquire(&p.lock);
      if (wpid == -1 || wpid == p.pid) {
        havekids = 1;
        if(p.get_state() == ZOMBIE){
          release(&p.lock);	// no one else better be trying to lock p
          pid = p.pid;
          myproc()->childq.erase(it);
          release(&myproc()->lock);

          if (status) {
            status.store(&p.status);
          }

          finishproc(&p);
          return pid;
        }
      }
      release(&p.lock);
    }

    // No point waiting if we don't have any children.
    if(!havekids || myproc()->killed){
      release(&myproc()->lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    myproc()->cv->sleep(&myproc()->lock);
    release(&myproc()->lock);
  }
}

void
threadhelper(void (*fn)(void *), void *arg)
{
  post_swtch();
  mtstart(fn, myproc());
  fn(arg);
  exit(0);
}

static struct proc*
threadalloc(void (*fn)(void *), void *arg)
{
  struct proc *p;

  p = proc::alloc();
  if (p == nullptr)
    return 0;

  auto proc_cleanup = scoped_cleanup([&p]() {
    if (!xnspid->remove(p->pid, &p))
      panic("fork: ns_remove");
    freeproc(p);
  });

  p->vmap = vmap::alloc();
  if (p->vmap == nullptr)
    return 0;

  p->init_vmap();

  // XXX can threadstub be deleted?
  p->context->rip = (u64)threadstub;
  p->context->r12 = (u64)fn;
  p->context->r13 = (u64)arg;
  p->parent = nullptr;
  p->cwd.reset();

  proc_cleanup.dismiss();
  return p;
}

struct proc*
threadrun(void (*fn)(void*), void *arg, const char *name)
{
  struct proc *p = threadalloc(fn, arg);
  if (p == nullptr)
    panic("threadrun: alloc");

  snprintf(p->name, sizeof(p->name), "%s", name);
  acquire(&p->lock);
  addrun(p);
  release(&p->lock);
  return p;
}

struct proc*
threadpin(void (*fn)(void*), void *arg, const char *name, int cpu)
{
  struct proc *p = threadalloc(fn, arg);
  if (p == nullptr)
    panic("threadpin: alloc");

  snprintf(p->name, sizeof(p->name), "%s", name);
  p->cpuid = cpu;
  p->cpu_pin = 1;
  acquire(&p->lock);
  addrun(p);
  release(&p->lock);
  return p;
}

bool
proc::deliver_signal(int signo)
{
  if (signo < 0 || signo >= NSIG)
    return false;

  if (sig[signo].sa_handler == 0)
    return false;

  trapframe tf_save = *tf;
  tf->rsp -= 128;   // skip redzone
  tf->rsp -= sizeof(tf_save);
  if (putmem((void*) tf->rsp, &tf_save, sizeof(tf_save)) < 0)
    return false;

  tf->rsp -= 8;
  if (putmem((void*) tf->rsp, &sig[signo].sa_restorer, 8) < 0)
    return false;

  tf->rip = (u64) sig[signo].sa_handler;
  tf->rdi = signo;
  return true;
}

