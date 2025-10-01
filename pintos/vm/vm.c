/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "include/threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/inspect.h"

struct list frame_list;
struct lock frame_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();
#ifdef EFILESYS /* For project 4 */
  pagecache_init();
#endif
  register_inspect_intr();
  /* DO NOT MODIFY UPPER LINES. */
  list_init(&frame_list);
  lock_init(&frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
    case VM_UNINIT:
      return VM_TYPE(page->uninit.type);
    default:
      return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* 초기화 함수를 가진 "대기(pending)" 페이지 객체를 생성한다.
 * 새로운 페이지를 만들고 싶다면, 직접 생성하지 말고
 * 반드시 이 함수나 `vm_alloc_page`를 통해 생성해야 한다.
 * aux free 책임을 어디서 질건지 체크 */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {
  // 외부에서 uninit type 날리면 안됨.
  ASSERT(VM_TYPE(type) != VM_UNINIT)

  struct supplemental_page_table *spt = &thread_current()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL) {
      goto err;
    }

    page->va = upage;
    page->writable = writable;

    bool (*initializer)(struct page *, enum vm_type, void *kva) = NULL;
    switch (VM_TYPE(type)) {
      case VM_ANON:
        initializer = anon_initializer;
        break;
      case VM_FILE:
        initializer = file_backed_initializer;
        break;

      default:
        free(page);
        goto err;
    }

    uninit_new(page, upage, init, type, aux, initializer);
    if (!spt_insert_page(spt, page)) {
      free(page);
      goto err;
    }
  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED,
                           void *va UNUSED) {
  // va만 삽입한 가짜 hash_elem을 보내는 방식
  /* TODO: Fill this function. */
  struct page *page = NULL;
  struct hash_elem *e = NULL;

  page->va = pg_round_down(va);
  e = hash_find(&spt->page_table, &page->hash_elem);
  if (e != NULL) {
    page = hash_entry(e, struct page, hash_elem);
  }
  return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page) {
  int succ = false;

  struct hash_elem *result_elem =
      hash_insert(&spt->page_table, &page->hash_elem);

  if (result_elem == &page->hash_elem) {
    succ = true;
  }

  return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  struct hash_elem *he = hash_delete(&spt->page_table, &page->hash_elem);
  if (he != NULL) {
    // double free 위험
    vm_dealloc_page(page);
  }
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */

  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
  struct frame *victim UNUSED = vm_get_victim();
  /* TODO: swap out the victim and return the evicted frame. */

  return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  void *kva = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kva == NULL) {
    frame = vm_evict_frame();
  } else {
    frame = malloc(sizeof(struct frame));
    if (frame == NULL) {
      palloc_free_page(kva);
    }
    frame->kva = kva;
    frame->page = NULL;
    // list_insert(frame->elem,
    // todo : 나중에 LRU func만들고 list_insert_ordered하기
  }

  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED,
                         bool not_present UNUSED) {
  struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
  struct page *page = NULL;
  if (addr == NULL || is_kernel_vaddr(addr) || not_present) {
    return false;
  }

  page = spt_find_page(spt, addr);
  if (page == NULL) {
    return false;
  }

  return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
  page_destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
  struct page *page = NULL;
  /* TODO: Fill this function */

  return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
  struct frame *frame = vm_get_frame();

  frame->page = page;
  page->frame = frame;

  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);

  return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */

void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  ASSERT(spt != NULL);
  hash_init(&spt->page_table, hash_hash_func, hash_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt) {
  ASSERT(spt != NULL);
  hash_clear(&spt->page_table, destruct_hash_elem);
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
}

static uint64_t hash_hash_func(const struct hash_elem *e, void *aux) {
  struct page *p = hash_entry(e, struct page, hash_elem);
  return hash_bytes(&p->va, sizeof(p->va));
}

static bool hash_less_func(const struct hash_elem *a, const struct hash_elem *b,
                           void *aux) {
  struct page *p1 = hash_entry(a, struct page, hash_elem);
  struct page *p2 = hash_entry(a, struct page, hash_elem);
  return p1->va < p2->va;
}
