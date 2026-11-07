#include "hos_fault.h"
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
static char *page;
static volatile sig_atomic_t repaired, chained;
static int repair(HosFault *f) {
    if (f->signal != SIGSEGV || !f->write || f->address != (uintptr_t)page) return 0;
    assert(f->pc && f->context);
    if (mprotect(page,4096,PROT_READ|PROT_WRITE)) return 0;
    repaired++;
    return 1;
}
static void prior(int sig) { assert(sig == SIGTRAP); chained++; }
static void *reader(void *p) {
    char out[32];
    for(int i=0;i<100;i++) {
        assert(hos_guarded_copy(out,p,32));
        assert(!hos_guarded_copy(out,(void*)1,32));
    }
    return NULL;
}
int main(void) {
    signal(SIGTRAP,prior);
    assert(hos_fault_add(repair));
    page=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(page!=MAP_FAILED);
    strcpy(page,"native guarded copy");
    pthread_t t[4]; for(int i=0;i<4;i++) assert(!pthread_create(&t[i],NULL,reader,page));
    for(int i=0;i<4;i++) assert(!pthread_join(t[i],NULL));
    assert(!mprotect(page+4096,4096,PROT_NONE));
    char out[32]; assert(!hos_guarded_copy(out,page+4090,32));
    assert(!mprotect(page,4096,PROT_READ));
    *(volatile char *)page='N'; assert(repaired==1 && *(volatile char *)page=='N');
    raise(SIGTRAP); assert(chained==1);
    pid_t child=fork(); assert(child>=0);
    if(!child) { *(volatile char *)(page+4096)=1; _exit(99); }
    int status; assert(waitpid(child,&status,0)==child); assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGSEGV);
    munmap(page,8192);
    puts("native fault recovery, chaining, guarded copies and fatal delivery passed");
}
