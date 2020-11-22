#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <sync.h>

//pre define
struct mm_struct;

// the virtual continuous memory area(vma), [vm_start, vm_end), 
// addr belong to a vma means  vma.vm_start<= addr <vma.vm_end 
struct vma_struct {
    struct mm_struct *vm_mm; // the set of vma using the same PDT 
    uintptr_t vm_start;      // start addr of vma ,PGSIZE对齐的      
    uintptr_t vm_end;        // end addr of vma, not include the vm_end itself,PGSIZE对齐的
    uint32_t vm_flags;       // flags of vma => VM_READ、VM_WRITE、VM_EXEC
    list_entry_t list_link;  // linear list link which sorted by start addr of vma => 将一系列用vma_struct表示的虚拟内存空间链接起来
};

#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

#define VM_READ                 0x00000001
#define VM_WRITE                0x00000002
#define VM_EXEC                 0x00000004

// the control struct for a set of vma using the same PDT
struct mm_struct {
    list_entry_t mmap_list;        // 链接mm_struct对应的所有vma_struct
    struct vma_struct *mmap_cache; // 指向当前正在使用的vma_struct(即段)
                                   // 由于局部性原理,当前正在使用的虚拟地址空间接下来可能还会使用,这是就不需要查链表,而是直接使用这个之指针)
                                   // 从而加快查询速度
    pde_t *pgdir;                  // the PDT of these vma => 指向mm_struct维护的页目录表(实际上就是boot_pgdir)
    int map_count;                 // the count of these vma => 记录mmap_list中链接的vma_struct的个数
    void *sm_priv;                 // the private data for swap manager 
                                   //  => 指向用来链接记录页访问情况的链表头(从而建立了mm_struct和swap_mmanager之间的联系)
                                   // 见swap_fifo.c下的_fifo_init_mm函数
};

struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags);
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);

void vmm_init(void);

int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);

extern volatile unsigned int pgfault_num;
extern struct mm_struct *check_mm_struct;
#endif /* !__KERN_MM_VMM_H__ */

