#include <swap.h>
#include <swapfs.h>
#include <swap_fifo.h>
#include <stdio.h>
#include <string.h>
#include <memlayout.h>
#include <pmm.h>
#include <mmu.h>
#include <default_pmm.h>
#include <kdebug.h>

/*****************************************************************************
 *                            物理页面置换公共接口
 * 1.关键函数
 *    swap_out()、swap_in()
 * 2.
 * ***************************************************************************/


// the valid vaddr for check is between 0~CHECK_VALID_VADDR-1
#define CHECK_VALID_VIR_PAGE_NUM 5
#define BEING_CHECK_VALID_VADDR 0X1000
#define CHECK_VALID_VADDR (CHECK_VALID_VIR_PAGE_NUM+1)*0x1000
// the max number of valid physical page for check
#define CHECK_VALID_PHY_PAGE_NUM 4
// the max access seq number
#define MAX_SEQ_NO 10

// 全局变量:页面置换管理器
// ucore采用的是swap_fifo.c中FIFO置换算法
static struct swap_manager *sm;

size_t max_swap_offset;

volatile int swap_init_ok = 0;

unsigned int swap_page[CHECK_VALID_VIR_PAGE_NUM];

unsigned int swap_in_seq_no[MAX_SEQ_NO],swap_out_seq_no[MAX_SEQ_NO];

static void check_swap(void);

/**
 *        初始化swap(被kern_init调用)
 * 1.初始化文件系统中的swap分区=> swapfs_init()
 * 2.完成页面置换管理器的初始化(对于FIFO而言,就是初始化队列)
 */ 
int swap_init(void)
{
     swapfs_init();

     if (!(1024 <= max_swap_offset && max_swap_offset < MAX_SWAP_OFFSET_LIMIT))
     {
          panic("bad max_swap_offset %08x.\n", max_swap_offset);
     }
     

     sm = &swap_manager_fifo;
     int r = sm->init();
     
     if (r == 0)
     {
          swap_init_ok = 1;
          cprintf("SWAP: manager = %s\n", sm->name);
          check_swap();
     }

     return r;
}

/**
 * 初始化mm_struct的成员sm_priv
 * 从而将该进程与其在内存中的页面联系起来 
 */
int swap_init_mm(struct mm_struct *mm)
{
     return sm->init_mm(mm);
}


/**
 * 操作可以为空
 */ 
int swap_tick_event(struct mm_struct *mm)
{
     return sm->tick_event(mm);
}

/***
 * 调用swap_manager的map_swappable()函数,将page加入置换队列
 * - mm:当前进程的mm_struct
 * - addr:数据的虚拟地址
 * - page:数据对应的物理内存页
 * - swap_in:换入内存/换出内存的标志
 * **/
int swap_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
     return sm->map_swappable(mm, addr, page, swap_in);
}

/**
 * 标记addr对应的页面不能被置换出去
 * => 目前是空操作
 */ 
int swap_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
     return sm->set_unswappable(mm, addr);
}

volatile unsigned int swap_out_num=0;


/**
 * 从进程(mm_struct)的物理页面中,选出n个,换出到磁盘
 * !!!注:需要修改二级页表项 => 由物理页号 修改为  磁盘地址; PTE_P也要修改
 */ 
int swap_out(struct mm_struct *mm, int n, int in_tick)
{
     int i;
     for (i = 0; i != n; ++ i)
     {
          uintptr_t v;
          //struct Page **ptr_page=NULL;
          struct Page *page;
          // cprintf("i %d, SWAP: call swap_out_victim\n",i);
          // 选出牺牲页
          int r = sm->swap_out_victim(mm, &page, in_tick);
          if (r != 0) {
                    cprintf("i %d, swap_out: call swap_out_victim failed\n",i);
                  break;
          }          
          //assert(!PageReserved(page));

          //cprintf("SWAP: choose victim page 0x%08x\n", page);
          
          v=page->pra_vaddr;          // 牺牲页的虚拟地址
          pte_t *ptep = get_pte(mm->pgdir, v, 0);          // 二级页表项
          assert((*ptep & PTE_P) != 0);

          // 将该页的内容写到磁盘
          if (swapfs_write( (page->pra_vaddr/PGSIZE+1)<<8, page) != 0) {
                    cprintf("SWAP: failed to save\n");
                    sm->map_swappable(mm, v, page, 0);
                    continue;
          }
          else {
                    cprintf("swap_out: i %d, store page in vaddr 0x%x to disk swap entry %d\n", i, v, page->pra_vaddr/PGSIZE+1);
                    *ptep = (page->pra_vaddr/PGSIZE+1)<<8;  // 将二级页表项内容由物理页号改为磁盘地址
                    free_page(page);
          }
          
          tlb_invalidate(mm->pgdir, v);
     }
     return i;
}


/**
 * 换入进程(mm_struct)虚拟地址addr对应的磁盘页面
 */ 
int swap_in(struct mm_struct *mm, uintptr_t addr, struct Page **ptr_result)
{
     struct Page *result = alloc_page();
     assert(result!=NULL);

     pte_t *ptep = get_pte(mm->pgdir, addr, 0);
     // cprintf("SWAP: load ptep %x swap entry %d to vaddr 0x%08x, page %x, No %d\n", ptep, (*ptep)>>8, addr, result, (result-pages));
    
     int r;
     if ((r = swapfs_read((*ptep), result)) != 0)
     {
        assert(r!=0);
     }
     cprintf("swap_in: load disk swap entry %d with swap_page in vadr 0x%x\n", (*ptep)>>8, addr);
     *ptr_result=result;
     return 0;
}



// 被check_swap()调用
static inline void check_content_set(void)
{
     *(unsigned char *)0x1000 = 0x0a;
     assert(pgfault_num==1);
     *(unsigned char *)0x1010 = 0x0a;
     assert(pgfault_num==1);
     *(unsigned char *)0x2000 = 0x0b;
     assert(pgfault_num==2);
     *(unsigned char *)0x2010 = 0x0b;
     assert(pgfault_num==2);
     *(unsigned char *)0x3000 = 0x0c;
     assert(pgfault_num==3);
     *(unsigned char *)0x3010 = 0x0c;
     assert(pgfault_num==3);
     *(unsigned char *)0x4000 = 0x0d;
     assert(pgfault_num==4);
     *(unsigned char *)0x4010 = 0x0d;
     assert(pgfault_num==4);
}

// 被check_swap()调用
static inline int check_content_access(void)
{
    int ret = sm->check_swap();
    return ret;
}

struct Page * check_rp[CHECK_VALID_PHY_PAGE_NUM];
pte_t * check_ptep[CHECK_VALID_PHY_PAGE_NUM];
unsigned int check_swap_addr[CHECK_VALID_VIR_PAGE_NUM];

extern free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)


/**
 * 检验换入换出机制
 * 不过检验时使用的check_mm_struct不属于任何进程,只是起到模拟的作用 
 * 调用关系:kern_init -> swap_init -> check_swap
 *   => ucore的用户线程实际运行时不会换入换出(不过可以自行实现!)
 */ 
static void check_swap(void)
{
    //backup mem env
     int ret, count = 0, total = 0, i;
     list_entry_t *le = &free_list;
     while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
     }
     assert(total == nr_free_pages());
     cprintf("BEGIN check_swap: count %d, total %d\n",count,total);
     
     //now we set the phy pages env     
     struct mm_struct *mm = mm_create();
     assert(mm != NULL);

     extern struct mm_struct *check_mm_struct;
     assert(check_mm_struct == NULL);

     check_mm_struct = mm;

     pde_t *pgdir = mm->pgdir = boot_pgdir;
     assert(pgdir[0] == 0);

     struct vma_struct *vma = vma_create(BEING_CHECK_VALID_VADDR, CHECK_VALID_VADDR, VM_WRITE | VM_READ);
     assert(vma != NULL);

     insert_vma_struct(mm, vma);

     //setup the temp Page Table vaddr 0~4MB
     cprintf("setup Page Table for vaddr 0X1000, so alloc a page\n");
     pte_t *temp_ptep=NULL;
     temp_ptep = get_pte(mm->pgdir, BEING_CHECK_VALID_VADDR, 1);
     assert(temp_ptep!= NULL);
     cprintf("setup Page Table vaddr 0~4MB OVER!\n");
     
     for (i=0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
          check_rp[i] = alloc_page();
          assert(check_rp[i] != NULL );
          assert(!PageProperty(check_rp[i]));
     }
     list_entry_t free_list_store = free_list;
     list_init(&free_list);
     assert(list_empty(&free_list));
     
     //assert(alloc_page() == NULL);
     
     unsigned int nr_free_store = nr_free;
     nr_free = 0;
     for (i=0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
        free_pages(check_rp[i],1);
     }
     assert(nr_free==CHECK_VALID_PHY_PAGE_NUM);
     
     cprintf("set up init env for check_swap begin!\n");
     //setup initial vir_page<->phy_page environment for page relpacement algorithm 

     
     pgfault_num=0;
     
     check_content_set();
     assert( nr_free == 0);         
     for(i = 0; i<MAX_SEQ_NO ; i++) 
         swap_out_seq_no[i]=swap_in_seq_no[i]=-1;
     
     for (i= 0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
         check_ptep[i]=0;
         check_ptep[i] = get_pte(pgdir, (i+1)*0x1000, 0);
         //cprintf("i %d, check_ptep addr %x, value %x\n", i, check_ptep[i], *check_ptep[i]);
         assert(check_ptep[i] != NULL);
         assert(pte2page(*check_ptep[i]) == check_rp[i]);
         assert((*check_ptep[i] & PTE_P));          
     }
     cprintf("set up init env for check_swap over!\n");
     // now access the virt pages to test  page relpacement algorithm 
     ret=check_content_access();
     assert(ret==0);
     
     //restore kernel mem env
     for (i=0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
         free_pages(check_rp[i],1);
     } 

     //free_page(pte2page(*temp_ptep));
    free_page(pde2page(pgdir[0]));
     pgdir[0] = 0;
     mm->pgdir = NULL;
     mm_destroy(mm);
     check_mm_struct = NULL;
     
     nr_free = nr_free_store;
     free_list = free_list_store;

     
     le = &free_list;
     while ((le = list_next(le)) != &free_list) {
         struct Page *p = le2page(le, page_link);
         count --, total -= p->property;
     }
     cprintf("count is %d, total is %d\n",count,total);
     //assert(count == 0);
     
     cprintf("check_swap() succeeded!\n");
}
