/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "vm/inspect.h"
// 추가한부분
#include "threads/vaddr.h"
#include "threads/mmu.h"
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
  // 외부에서 uninit type 날리면 안됨. ASSERT(VM_TYPE(type) != VM_UNINIT)

  struct supplemental_page_table *spt = &thread_current()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL) {
      goto err;
    }
    /* 페이지의 가상주소와 권한을 할당 */
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

    /* uninit 페이지로 초기화 후 spt테이블 삽입 */
    uninit_new(page, upage, init, type, aux, initializer);
    page->writable = writable;

    if (!spt_insert_page(spt, page)) {
      free(page);
      goto err;
    }
  }
  /* 성공했으면 true 반환 */
  return true;

err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */

struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
  // va만 삽입한 가짜 hash_elem을 보내는 방식
  /* TODO: Fill this function. */
  /* va만 저장할 껍데기 페이지 */
  struct page temp_page;

  /* 찾을 페이지의 elem*/
  struct hash_elem *e = NULL;

  /* e를 이용해 복구될 페이지 */
  struct page *page = NULL;

  temp_page.va = pg_round_down(va);
  e = hash_find(&spt->page_table, &temp_page.hash_elem);
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

  if (result_elem == NULL) {
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
 * space.
 * 프레임을 malloc, kva를 palloc 하고 반환*/
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  void *kva = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kva == NULL) {
    return vm_evict_frame();
  }

  frame = malloc(sizeof(struct frame));
  if (frame == NULL) {
    palloc_free_page(kva);
  }
  frame->kva = kva;
  frame->page = NULL;
  // list_insert(frame->elem,
  // todo : 나중에 LRU func만들고 list_insert_ordered하기

  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr, bool user,
                         bool write, bool not_present) {
  /* addr 없으면 false*/
  if (addr == NULL) return false;
  /* 유저영역주소가 아니라면 (커널) false)*/
  if (!is_user_vaddr(addr)) return false;
  /* PTE가 있는데 들어왔다면 false */
  if (!not_present) return false;

  struct supplemental_page_table *spt = &thread_current()->spt;
  struct page *page = NULL;
  page = spt_find_page(spt, addr);

  /* page가 없으면 false */
  if (page == NULL) return false;
  /* writable은 false인데 write가 true로 오면 false*/
  if (!page->writable && write) return false;

  return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
  page_destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
// GitBook : 주어진 VA에 페이지를 할당하고 해당 페이지에 프레임 할당
// 근데 생각해보면 이미 page가 vm_alloc_page_with_initializer에서 malloc되어
// 있을텐데?
bool vm_claim_page(void *va) {
  struct page *page = NULL;

  if (va == NULL || is_kernel_vaddr(va)) {
    return false;
  }
  // spt를 넣어야함.
  page = spt_find_page(&thread_current()->spt, va);
  if (page == NULL) {
    return false;
  }

  bool is_success = vm_do_claim_page(page);  // 페이지에 프레임 할당
  if (!is_success) {
    free(page);
  }

  return is_success;
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
  struct frame *frame = vm_get_frame();

  frame->page = page;
  page->frame = frame;

  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva,
                     page->writable))
    return false;

  return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */

void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  ASSERT(spt != NULL);
  hash_init(&spt->page_table, page_hash, page_less, NULL);
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