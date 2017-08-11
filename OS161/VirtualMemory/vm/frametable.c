#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>

/* 
 * Place your frametable data-structures here 
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */

/* Note that this function returns a VIRTUAL address, not a physical 
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */

/*
 * Make variables static to prevent it from other file's accessing
 */
struct spinlock frametable_lock = SPINLOCK_INITIALIZER;

/*
 * Allocation function for public accessing
 * Returning virtual address of frame
 */
vaddr_t
alloc_kpages(unsigned int npages)
{
        paddr_t paddr;
        struct frame_table_entry *p;
        int i;
        spinlock_acquire(&frametable_lock);
        if (frame_table == 0){
                paddr = ram_stealmem(npages);
        }
        else{
                if (npages > 1){
                        spinlock_release(&frametable_lock);
                        return 0;
                }
                // Freeframe equals zero means all the frames have been allocated
                // and there is no frame to use.
                if (freeframe == 0){
                        spinlock_release(&frametable_lock);
                        return 0;
                }
                
                // Get the current free frame's entry id 
                // and retrieve the next free frame 
                paddr = freeframe;
                i = (freeframe - frametop) / PAGE_SIZE;
                p = frame_table + i;
                freeframe = p->next_freeframe;
                if(paddr==freeframe){
                        return 0;
                }
                p->next_freeframe = 0;//set used
        }
        spinlock_release(&frametable_lock);
        if(paddr == 0)
                return 0;
        return PADDR_TO_KVADDR(paddr);
}

/*
 * Free page function for public accessing
 */
void
free_kpages(vaddr_t addr)
{
        KASSERT(addr >= MIPS_KSEG0);
        struct frame_table_entry *p;
        int i;
        paddr_t paddr = KVADDR_TO_PADDR(addr);
        if (paddr <= frametop) {
                // memory leakage
        }
        else {
                spinlock_acquire(&frametable_lock);
                i = (paddr - frametop) / PAGE_SIZE;
                p = frame_table + i;
                p->next_freeframe = freeframe;
                freeframe = paddr;
                spinlock_release(&frametable_lock);
        }
}