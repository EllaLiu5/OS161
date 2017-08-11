/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *  The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spl.h>
#include <spinlock.h>
#include <elf.h>
#include <proc.h>


struct addrspace *
as_create(void)
{
    struct addrspace *as;
    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }
    as->as_regions_start = NULL;
    return as;
}

/*
 * Copy all the contents of the old addrspace to the new addrspace.
 * Both the page frames mapped in the two-level page table of
 * the old addrspace to the new addrspace and the regions keeped in 
 * the old one.
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *new;
    struct as_region *re, *newre, *oldre;
    int ret_value;
    // initialise the new addrspace
    new = as_create();
    if (new==NULL) {
        return ENOMEM;
    }
    re = old->as_regions_start;
    if (re == 0) {
        return 0;
    }
    newre = kmalloc(sizeof(struct as_region));
    KASSERT(re->as_vbase != 0);
    KASSERT(re->as_npages != 0);
    newre->as_vbase = re->as_vbase;
    newre->as_npages = re->as_npages;
    newre->as_permissions = re->as_permissions;
    newre->as_next_region = 0;
    new->as_regions_start = newre;
    re = re->as_next_region;
    //copy all the regions to the new as
    while (re != 0) {
        oldre = newre;
        newre = kmalloc(sizeof(struct as_region));
        KASSERT(re->as_vbase != 0);
        KASSERT(re->as_npages != 0);
        newre->as_vbase = re->as_vbase;
        newre->as_npages = re->as_npages;
        newre->as_permissions = re->as_permissions;
        newre->as_next_region = 0;
        oldre->as_next_region = newre;
        re = re->as_next_region;
    }  
    // copy the contents of the old two-level page table
    // to the new one
    ret_value = copyPageTable((uint32_t)old, (uint32_t)new);
    if (ret_value) return ret_value;
    *ret = new;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
    delete_page_table_entry((uint32_t)as);
    kfree(as);
}

/*
 * Frobe TLB table
 */
void
as_activate(void)
{
    int spl = splhigh();
    for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. 
 *
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
         int readable, int writeable, int executable)
{
    size_t npages;
    struct as_region *ar;
    
    KASSERT(as != NULL);
    
    // Align the region. First, the base.
    sz += vaddr & ~(vaddr_t)PAGE_FRAME;//size+startoffset
    vaddr &= PAGE_FRAME;//page vaddr
    
    // now the length.
    sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME; //meaning?
    npages = sz / PAGE_SIZE;
    
    // Store the region base, size and permissions
    if (as->as_regions_start == NULL) {
        as->as_regions_start = kmalloc(sizeof(struct as_region));
        as->as_regions_start->as_vbase = vaddr;
        as->as_regions_start->as_npages = npages;
        as->as_regions_start->as_permissions = readable | writeable | executable;
        as->as_regions_start->as_next_region = 0;
        if(as->as_regions_start == NULL){
            panic("Region cannot be created");
            return ENOMEM;
        }
    }
    else {
        ar = as->as_regions_start;
        while (ar->as_next_region != 0) {
            ar = ar->as_next_region;
        }
        ar->as_next_region = kmalloc(sizeof(struct as_region));
        ar->as_next_region->as_vbase = vaddr;
        ar->as_next_region->as_npages = npages;
        ar->as_next_region->as_permissions = readable | writeable | executable;
        ar->as_next_region->as_next_region = 0;
        if(ar->as_next_region == NULL){
            panic("Region cannot be created");
            return ENOMEM;
        }
    }
    return 0;
}

/*
 * Assign the writable permission to all region in order for os 
 * to load segment into physical frame
 */
int
as_prepare_load(struct addrspace *as)
{
    KASSERT(as != NULL);
    KASSERT(as->as_regions_start != 0);
    struct as_region *re;
    unsigned int permis = 0;
    
    re = as->as_regions_start;
    while (re != 0) {
        permis = re->as_permissions;
        // in order to restore the permission flag back when 
        // the load execution done, move the original permission
        // 8 bit left to reserve it. Then put the new permission flag
        // into the last 8 bit
        re->as_permissions <<= 8;
        re->as_permissions = re->as_permissions | permis | PF_W;//writable
        re = re->as_next_region;
    }
    return 0;
}

/*
 * Restore the original region permission flag back
 */
int
as_complete_load(struct addrspace *as)
{
    KASSERT(as != NULL);
    KASSERT(as->as_regions_start != 0);
    
    struct as_region *s;
    s = as->as_regions_start;
    while (s != 0) {
        s->as_permissions >>= 8;
        s = s->as_next_region;
    }
    return 0;
}

/*
 * Set up the mapping from stack virtual address to the page table
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    (void)as;
    // Initial user-level stack pointer
    *stackptr = USERSTACK;
    return 0;
}

/*
 * Zero out a page within the provide address
 */
void
as_zero_region(vaddr_t vaddr, unsigned npages)
{
    bzero((void *)vaddr, npages * PAGE_SIZE);
}

void
as_deactivate(void)
{
        /*
         * Write this. For many designs it won't need to actually do
         * anything. See proc.c for an explanation of why it (might)
         * be needed.
         */
}