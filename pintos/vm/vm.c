/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "vm/inspect.h"
// 추가한부분
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"  // setup_stack
#include "string.h"

static bool valid_stack_growth(struct intr_frame *f, void *addr);
static bool vm_stack_growth(void *addr);
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
    frame = vm_evict_frame();
  } else {
    frame = malloc(sizeof(struct frame));
    if (frame == NULL) {
      palloc_free_page(kva);
    }
    frame->kva = kva;
    frame->page = NULL;
  }
  // list_insert(frame->elem,
  // todo : 나중에 LRU func만들고 list_insert_ordered하기

  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;
}

/* Growing the stack. */
static bool vm_stack_growth(void *addr) {
  struct thread *t = thread_current();
  void *stack_bottom = (void *)t->stack_bottom;
  void *ksp = pg_round_down(addr);
  // 익명 페이지를 만들어서 성장시켜라
  // 1. 이 주소에 대해서 익명으로 할당하고 매핑
  while (stack_bottom > ksp) {
    stack_bottom = (void *)(((uint8_t *)stack_bottom) - PGSIZE);
    if (!vm_alloc_page_with_initializer(VM_MARKER_0 | VM_ANON, stack_bottom,
                                        true, NULL, NULL))
      return false;

    if (!vm_claim_page(stack_bottom)) return false;
  }
  t->stack_bottom = stack_bottom;
  return true;
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
                         bool write, bool not_present) {
  /* addr 없으면 false*/
  if (addr == NULL) return false;
  /* 유저영역주소가 아니라면 (커널) false)*/
  if (!is_user_vaddr(addr)) return false;
  /* PTE가 있는데 들어왔다면 false */
  if (!not_present) return false;

  struct thread *t = thread_current();
  struct supplemental_page_table *spt = &t->spt;

  // 1. 매핑된 페이지가 있다면  그걸 스왑인
  struct page *page = spt_find_page(spt, addr);
  if (page != NULL) return vm_do_claim_page(page);

  // 2. 없으면 “스택 확장 조건”을 검사
  if (valid_stack_growth(f, addr)) return vm_stack_growth(addr);

  // 3. 그 외는 fault 처리 불가
  return false;
}

static bool valid_stack_growth(struct intr_frame *f, void *addr) {
  // 최상단과 최하단사이
  return (addr >= f->rsp - 32 && addr < USER_STACK &&
          addr >= USER_STACK - (1 << 20));
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

  if (va == NULL || is_kernel_vaddr(va)) return false;

  // spt를 넣어야함.
  page = spt_find_page(&thread_current()->spt, va);
  if (page == NULL) return false;

  bool is_success = vm_do_claim_page(page);  // 페이지에 프레임 할당
  if (!is_success) free(page);

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
/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst,
                                  struct supplemental_page_table *src) {
  struct hash_iterator i;
  hash_first(&i, &src->page_table);

  while (hash_next(&i)) {
    struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);
    enum vm_type t = VM_TYPE(parent_page->operations->type);

    void *pva = parent_page->va;
    bool p_wrt = parent_page->writable;

    switch (t) {
      case VM_UNINIT:

        struct lazy_load_aux *p_aux = parent_page->uninit.aux;
        struct lazy_load_aux *c_aux = malloc(sizeof(struct lazy_load_aux));

        vm_initializer *init_fn = parent_page->uninit.init;
        if (!c_aux) return false;
        *c_aux = (struct lazy_load_aux){
            .file = file_reopen(p_aux->file),
            .ofs = p_aux->ofs,
            .page_read_bytes = p_aux->page_read_bytes,
            .page_zero_bytes = p_aux->page_zero_bytes,
        };

        if (!vm_alloc_page_with_initializer(page_get_type(parent_page), pva,
                                            p_wrt, init_fn, c_aux)) {
          file_close(c_aux->file);
          free(c_aux);
          return false;
        }
        break;

      case VM_ANON:
      case VM_FILE:
        // 여기서 페이지새로 만들고, 새 프레임할당.
        if (!(vm_alloc_page(t, pva, p_wrt) && vm_claim_page(pva))) {
          return false;
        }

        struct page *c_page = spt_find_page(dst, pva);
        if (!c_page) return false;

        memcpy(c_page->frame->kva, parent_page->frame->kva, PGSIZE);
        break;
      default:
        return false;
        break;
    }
  }
  return true;

  // *page = (struct page){
  //     .operations = &uninit_ops,  // 일단 uninit 전용 operatoins 연결
  //     .va = va,                   // 페이지가 담당할 사용자 가상주소
  //     .frame = NULL,              // 실제 프레임은 NULL
  //     .uninit = (struct uninit_page){
  //         .init =
  //             init,  // lazy-load 때 실제 내용을 채울
  //             함수(vm_initializer같은..)
  //         .type = type,  // 최종 타입힌트 (anon, file)
  //         .aux = aux,    // init에 전달할 부가정보
  //         .page_initializer =
  //             initializer,  // uninit->실제타입으로 변환시키는 함수
  //     }};

  // struct lazy_load_aux *aux = malloc(sizeof(struct lazy_load_aux));
  //   aux->file = file;
  //   aux->page_read_bytes = page_read_bytes;
  //   aux->page_zero_bytes = page_zero_bytes;
  //   aux->ofs = ofs;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt) {
  ASSERT(spt != NULL);
  hash_clear(&spt->page_table, destruct_hash_elem);
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
}