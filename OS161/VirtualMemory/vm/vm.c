#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spinlock.h>
#include <elf.h>
#include <spl.h>
#include <proc.h>

/*
 * Initialise the frame table
 */

struct page_table_entry *page_table;
struct spinlock page_table_lock = SPINLOCK_INITIALIZER;
int framenum;

void
vm_bootstrap(void) 
{
    paddr_t firsta=0, lasta=0, paddr;
    int entry_num, frame_table_size, i, page_table_size;
    // get the useable range of physical memory
    lasta = ram_getsize();
    firsta = ram_getfirstfree();
    KASSERT((firsta & PAGE_FRAME) == firsta);
    KASSERT((lasta & PAGE_FRAME) == lasta);
    framenum = (lasta - firsta) / PAGE_SIZE;
    // calculate the size of the whole framemap
    frame_table_size = framenum * sizeof(struct frame_table_entry);
    frame_table_size = ROUNDUP(frame_table_size, PAGE_SIZE);
    entry_num = frame_table_size / PAGE_SIZE;
    //set the size of page table
    page_table_size = 2*framenum * sizeof(struct page_table_entry);
    page_table_size = ROUNDUP(page_table_size, PAGE_SIZE);
    entry_num += page_table_size / PAGE_SIZE;
    
    frametop = firsta;
    pagetop = firsta + frame_table_size;
    freeframe = firsta + frame_table_size + page_table_size;
    
    if (freeframe >= lasta) {
            // This is impossible for most of the time
            panic("vm: framemap consume physical memory?\n");
    }
    
    // keep the frame state in the top of the useable range of physical memory
    // the free frame page address started from the end of the frame map
    frame_table = (struct frame_table_entry *) PADDR_TO_KVADDR(firsta);
    page_table = (struct page_table_entry *) PADDR_TO_KVADDR(pagetop);
    
    // Initialise the frame list, each entry corrsponding to a frame,
    // and each entry stores the address of the next free frame.
    // If the next frame address of this entry equals zero, means this current frame is allocated
    for (i = 0; i < framenum-1; i++) {
        if (i < entry_num) {
            frame_table[i].next_freeframe = 0;
            continue;
        }
        paddr = frametop + (i+1) * PAGE_SIZE;
        frame_table[i].next_freeframe = paddr;
    }
    frame_table[framenum-1].next_freeframe = frametop + (entry_num) * PAGE_SIZE;
    for (i = 0; i < framenum*2; i++) {
        page_table[i].id=0;
    }
}

/*
 * When TLB miss happening, a page fault will be trigged.
 * The way to handle it is as follow:
 * 1. check what page fault it is, if it is READONLY fault, 
 *    then do nothing just pop up an exception and kill the process
 * 2. if it is a read fault or write fault
 *    1. first check whether this virtual address is within any of the regions
 *       or stack of the current addrspace. if it is not, pop up a exception and
 *       kill the process, if it is there, goes on. 
 *    2. then try to find the mapping in the page table, 
 *       if a page table entry exists for this virtual address insert it into TLB 
 *    3. if this virtual address is not mapped yet, mapping this address,
 *   update the pagetable, then insert it into TLB
 */
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    vaddr_t vaddr, vbase, vtop, faultadd = 0;
    paddr_t paddr;
    struct addrspace *as;
    struct as_region *re;
    uint32_t ehi, elo;
    int i, spl;
    int permis = 0;
    
    switch (faulttype) {
        case VM_FAULT_READONLY:
            return EFAULT;
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        default:
            return EINVAL;
    }
    
    as = proc_getas();
    if (as == NULL) {
        return EFAULT;
    }

    // Align faultaddress
    faultaddress &= PAGE_FRAME;
    
    // Go through the link list of regions 
    // Check the validation of the faultaddress
    KASSERT(as->as_regions_start != 0);
    re = as->as_regions_start;
    while (re != 0) {
        KASSERT(re->as_vbase != 0);
        KASSERT(re->as_npages != 0);
        KASSERT((re->as_vbase & PAGE_FRAME) == re->as_vbase);
        vbase = re->as_vbase;
        vtop = vbase + re->as_npages * PAGE_SIZE;
        if (faultaddress >= vbase && faultaddress < vtop) {
            faultadd = faultaddress;
            permis = re->as_permissions;
            break;
        }
        re = re->as_next_region;
    }

    //check if in user stack
    if (faultadd == 0) {
        vtop = USERSTACK;
        vbase = vtop - VM_STACKPAGES * PAGE_SIZE;
        if (faultaddress >= vbase && faultaddress < vtop) {
            faultadd = faultaddress;
            // Stack is readable, writable but not executable
            permis |= (PF_W | PF_R);
        }
        
        // faultaddress is not within any range of the regions and stack
        if (faultadd == 0) {
            return EFAULT;
        }
    }
    paddr = look_up_page_table((uint32_t)as, faultaddress);
    //not exist
    if(paddr == 0){
        vaddr = alloc_kpages(1);
        KASSERT(vaddr != 0);
        if (vaddr == 0){
            return ENOMEM;
        }
        as_zero_region(vaddr, 1);
        paddr = KVADDR_TO_PADDR(vaddr);
        page_table_insert((uint32_t)as, faultaddress, paddr);
        if (permis & PF_W) {
            paddr |= TLBLO_DIRTY;
        }
    }
    else{
        if (permis & PF_W) {
            paddr |= TLBLO_DIRTY;
        }
    }    
    spl = splhigh();
    // update TLB entry
    // if there still a empty TLB entry, insert new one in
    // if not, randomly select one, throw it, insert new one in
    for (i=0; i<NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        if (elo & TLBLO_VALID) {
            continue;
        }
        ehi = faultaddress;
        elo = paddr | TLBLO_VALID;
        tlb_write(ehi, elo, i);
        splx(spl);
        return 0;
    }
    ehi = faultaddress;
    elo = paddr | TLBLO_VALID;
    tlb_random(ehi, elo);
    splx(spl);
    return 0;
}

/*
 * SMP-specific functions.  Unused in our configuration.
 */
void
vm_tlbshootdown_all(void)
{
    panic("vm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    panic("vm tried to do tlb shootdown?!\n");
}

paddr_t look_up_page_table(uint32_t check_id, vaddr_t va) {
    struct page_table_entry * pte=page_table;
    paddr_t temp_p, pa;
    int i;
    KASSERT((va & PAGE_FRAME) == va);   
    // if page table has no entries, return NULL
    pa = KVADDR_TO_PADDR(va);
    i = (pa - frametop) / PAGE_SIZE;
    pte+=i;
    if (((pte->id) == check_id) && ((pte->page_addr) == va)) {
        temp_p = pte->frame_addr;
        return temp_p;
    }
    pte += framenum;
    if (((pte->id) == check_id) && ((pte->page_addr) == va)) {
        temp_p = pte->frame_addr;
        return temp_p;
    }
    pte=page_table;
    for (i = 0; i < framenum; i++) {
        if (((pte->id) == check_id) && ((pte->page_addr) == va)) {
            temp_p = pte->frame_addr;
            return temp_p;
        }
        pte += 1;
    }
    // last case, full page table scanned and entry not found
    return 0;     
}

struct page_table_entry * page_table_insert(uint32_t id, vaddr_t va, paddr_t pa) {
    int i;
    paddr_t paddr;
    struct page_table_entry * pte=page_table; 
    KASSERT(va < 0x80000000);
    KASSERT(pte != NULL); 
    paddr = KVADDR_TO_PADDR(va);
    i = (paddr - frametop) / PAGE_SIZE;
    pte += i;
    if (pte->id==0){
        pte->page_addr = va;
        pte->frame_addr = pa;
        pte->id = id;
        return pte;
    }
    pte += framenum;
    if(pte->id==0){
        pte->page_addr = va;
        pte->frame_addr = pa;
        pte->id = id;
        return pte;
    }
    pte=page_table;
    for (i = 0; i < framenum; i++) {
        if (pte->id==0) {
            break;
        }
        pte += 1;
    }
    if(pte->id==0){
        pte->page_addr = va;
        pte->frame_addr = pa;
        pte->id = id;
        return pte;
    }
    return 0;        
}

/* method to delete a particular page table entry */
void 
delete_page_table_entry(uint32_t id) {
    int i;
    struct page_table_entry * pte; 
    pte = page_table;
    KASSERT(pte != NULL); 
    for (i = 0; i < framenum*2; i++) {
        if (pte->id==id) {
            pte->id=0;
        }
        pte += 1;
    } 
}
int 
copyPageTable(uint32_t oldas, uint32_t newas) {
    struct page_table_entry * pte=page_table;
    int i;
    paddr_t paddr;
    vaddr_t vaddr, vaddr_new;
    for (i = 0; i < framenum*2; i++) {
        if (pte->id == oldas) {
            vaddr = pte->page_addr;
            vaddr_new = alloc_kpages(1);
            if (vaddr_new == 0){
                return ENOMEM;
            }
            as_zero_region(vaddr_new, 1);
            paddr = KVADDR_TO_PADDR(vaddr_new);
            page_table_insert(newas, vaddr, paddr);
            memcpy((void *)vaddr_new, (const void *) PADDR_TO_KVADDR(pte->frame_addr), PAGE_SIZE);
        }
        pte += 1;
    } 
    return 0;
}
