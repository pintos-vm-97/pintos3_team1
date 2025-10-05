/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "vm/inspect.h"
// 추가한부분
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"

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
enum vm_type page_get_type(struct page* page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
    case VM_UNINIT:
      return VM_TYPE(page->uninit.type);
    default:
      return ty;
  }
}

/* Helpers */
static struct frame* vm_get_victim(void);
static bool vm_do_claim_page(struct page* page);
static struct frame* vm_evict_frame(void);
static bool can_grow_stack(const struct intr_frame* f, void* addr,
                           bool is_user_mode);

/* 초기화 함수를 가진 "대기(pending)" 페이지 객체를 생성한다.
 * 새로운 페이지를 만들고 싶다면, 직접 생성하지 말고
 * 반드시 이 함수나 `vm_alloc_page`를 통해 생성해야 한다.
 * aux free 책임을 어디서 질건지 체크 */
/* init : 보통 lazy_load_segment 함수 넘어옴 */
bool vm_alloc_page_with_initializer(enum vm_type type, void* upage,
                                    bool writable, vm_initializer* init,
                                    void* aux) {
  // 외부에서 uninit type 날리면 안됨.
  // ASSERT(VM_TYPE(type) != VM_UNINIT);
  struct supplemental_page_table* spt = &thread_current()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    struct page* page = malloc(sizeof(struct page));
    if (page == NULL) {
      goto err;
    }
    /* 페이지의 가상주소와 권한을 할당 */
    page->va = upage;
    page->writable = writable;

    bool (*initializer)(struct page*, enum vm_type, void* kva) = NULL;
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

struct page* spt_find_page(struct supplemental_page_table* spt, void* va) {
  /* upage가 페이지의 시작이어야 함 */
  ASSERT(pg_ofs(va) == 0);
  /* va만 저장할 껍데기 페이지 */
  struct page temp_page;
  /* 찾을 페이지의 elem*/
  struct hash_elem* e = NULL;

  /* e를 이용해 복구될 페이지 */
  struct page* page = NULL;

  temp_page.va = va;
  e = hash_find(&spt->page_table, &temp_page.hash_elem);
  if (e != NULL) {
    page = hash_entry(e, struct page, hash_elem);
  }
  return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table* spt, struct page* page) {
  int succ = false;

  struct hash_elem* result_elem =
      hash_insert(&spt->page_table, &page->hash_elem);

  if (result_elem == NULL) {
    succ = true;
  }
  return succ;
}

void spt_remove_page(struct supplemental_page_table* spt, struct page* page) {
  struct hash_elem* he = hash_delete(&spt->page_table, &page->hash_elem);
  if (he != NULL) {
    // double free 위험
    vm_dealloc_page(page);
  }
}

/* Get the struct frame, that will be evicted. */
static struct frame* vm_get_victim(void) {
  struct frame* victim = NULL;
  /* TODO: The policy for eviction is up to you. */
  if (list_empty(&frame_list)) {
    return NULL;
  }

  // 나중에 frame_list 구조 수정 : 제일 오래 사용안된게 앞에 오도록 (accessed,
  // dirty bit 사용)
  return list_entry(list_begin(&frame_list), struct frame, elem);
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame* vm_evict_frame(void) {
  struct frame* victim UNUSED = vm_get_victim();
  if (victim == NULL) {
    return NULL;
  }
  /* TODO: swap out the victim and return the evicted frame. */
  // 해당 frame 참조하는 page들 참조 제거??

  return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.
 * 프레임을 malloc, kva를 palloc 하고 반환*/
static struct frame* vm_get_frame(void) {
  struct frame* frame = NULL;
  void* kva = palloc_get_page(PAL_USER | PAL_ZERO);
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
// stack 성장 경우 : ANON | STACK 페이지 생성 및 claim
static bool vm_stack_growth(void* addr UNUSED) {
  ASSERT(pg_ofs(addr) == 0);

  struct page* p = NULL;

  if (!vm_alloc_page_with_initializer(VM_ANON | VM_MARKER_0, addr, true, NULL,
                                      NULL)) {
    return false;
  }

  if (!vm_claim_page(addr)) {
    return false;
  }

  void* kva = pml4_get_page(thread_current()->pml4, addr);
  return kva != NULL;
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page* page UNUSED) { return true; }

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame* f, void* addr, bool user,
                         bool write, bool not_present) {
  /* addr 없거나 유저영역주소가 아니거나 PTE 존재하는 경우 false*/
  // stack성장 넣으면서 코드 길어질 것 같아서 if문 합침
  if (addr == NULL || !is_user_vaddr(addr) || !not_present) return false;

  struct supplemental_page_table* spt = &thread_current()->spt;
  struct page* page = NULL;
  void* upage = pg_round_down(addr);

  page = spt_find_page(spt, upage);

  if (page) {
    if (!page->writable && write) {
      return false;  // writable은 false인데 write가 true로 오면 false
    }

    return vm_do_claim_page(page);
  }
  // page가 없으면 stack 확장 여부 판단 필요
  return can_grow_stack(f, addr, user) ? vm_stack_growth(upage) : false;
}

// 스택 성장 조건 : 스택 bottot에서 어느정도 가깝고 최대스택크기 안 넘어야 됨
bool can_grow_stack(const struct intr_frame* f, void* addr, bool is_user_mode) {
  uintptr_t fault_addr = (uintptr_t)addr;  // fault address
  uintptr_t rsp =
      is_user_mode ? (uintptr_t)f->rsp : thread_current()->user_rsp_snap_shot;
  uintptr_t stack_top = (uintptr_t)USER_STACK;

  if (fault_addr >= stack_top) return false;  // 스택 위 접근
  if (fault_addr < rsp - 32) return false;    // 가까운지 먼지 (32byte Cuz 4B)

  uintptr_t stack_size = stack_top - fault_addr;
  if (stack_size > MAX_STACK_SIZE) return false;  // 최대 스택 크기 초과

  return true;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page* page) {
  page_destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
// GitBook : 주어진 VA에 페이지를 할당하고 해당 페이지에 프레임 할당
// 근데 생각해보면 이미 page가 vm_alloc_page_with_initializer에서 malloc되어
// 있을텐데?
bool vm_claim_page(void* va) {
  if (va == NULL || is_kernel_vaddr(va)) return false;

  struct page* page = spt_find_page(&thread_current()->spt, va);
  return page ? vm_do_claim_page(page) : false;  // 페이지에 프레임 할당
}

/* Claim the PAGE and set up the mmu. */
/*
 * Swapping구현 시 여기를 변경해야 하나?
 * 이전 : page 즉시 할당 -> page 없으면 걍 die
 * 변경해야 할 : 페이지 교체 알고리즘 활용하면 page할당. page없으면 swapping
 * 필요?
 */
static bool vm_do_claim_page(struct page* page) {
  struct frame* frame = vm_get_frame();

  frame->page = page;
  page->frame = frame;

  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva,
                     page->writable)) {
    return false;
  }
  // 연결 성공이니까 frame table추가?

  return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */

void supplemental_page_table_init(struct supplemental_page_table* spt UNUSED) {
  ASSERT(spt != NULL);
  hash_init(&spt->page_table, page_hash, page_less, NULL);
}

static struct lazy_load_aux* copy_aux(const struct lazy_load_aux* src) {
  struct lazy_load_aux* aux = malloc(sizeof(struct lazy_load_aux));
  if (aux == NULL) return NULL;
  aux->file = file_reopen(src->file);
  aux->page_read_bytes = src->page_read_bytes;
  aux->page_zero_bytes = src->page_zero_bytes;
  aux->ofs = src->ofs;
  aux->is_writable = src->is_writable;
  return aux;
}

static bool copy_uninit_page(struct supplemental_page_table* dst,
                             struct page* src) {
  struct lazy_load_aux* copied_aux = copy_aux(src->uninit.aux);
  if (copied_aux == NULL) return false;

  if (!vm_alloc_page_with_initializer(src->uninit.type, src->va, src->writable,
                                      src->uninit.init, copied_aux)) {
    file_close(copied_aux->file);
    free(copied_aux);
    return false;
  }

  return true;
}

/**
 * file-backed는 즉시로딩이긴한데 주의할 점 : reopen을 해야한다는 것
 * file.c의 initializer에서 reopen을 할까 고민을 했지만 그렇게 되면 처음 open도
 * reopen해야해서 이상함 그래서 어차피 올라온 memory는 memcpy쓰고 file포인터
 * 가진 부분만 교체하면 되니까 이런 방식 사용
 */
static bool copy_loaded_page(struct supplemental_page_table* dst,
                             struct page* src) {
  enum vm_type type = VM_TYPE(src->operations->type);
  void* va = src->va;
  struct page* dst_page = NULL;
  void* kva = NULL;

  if (!vm_alloc_page_with_initializer(type, va, src->writable, NULL, NULL)) {
    return false;
  }

  if (!vm_claim_page(va)) return false;

  dst_page = spt_find_page(dst, src->va);
  if (dst_page == NULL) return false;

  // 파일 페이지일 경우
  if (VM_TYPE(type) == VM_FILE) {
    dst_page->file.file = file_reopen(src->file.file);
  }

  kva = dst_page->frame->kva;
  memcpy(kva, src->frame->kva, PGSIZE);

  return true;
}

static bool copy_page(struct supplemental_page_table* dst, struct page* src) {
  bool copy_succ = false;
  enum vm_type type = VM_TYPE(src->operations->type);
  switch (type) {
    case VM_UNINIT:
      copy_succ = copy_uninit_page(dst, src);
      break;

    default:
      copy_succ = copy_loaded_page(dst, src);
  }
  return copy_succ;
}

/* Copy supplemental page table from src to dst */
// 부모의 SPT 엔트리를 순회하면서 lazy load 정보까지 복사한다. */
bool supplemental_page_table_copy(struct supplemental_page_table* dst UNUSED,
                                  struct supplemental_page_table* src UNUSED) {
  struct hash_iterator i;
  hash_first(&i, &src->page_table);

  while (hash_next(&i)) {
    struct page* page = hash_entry(hash_cur(&i), struct page, hash_elem);
    if (page == NULL) return false;

    if (!copy_page(dst, page)) {
      hash_clear(&dst->page_table, destruct_hash_elem);
      return false;
    }
  }

  return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table* spt) {
  ASSERT(spt != NULL);
  hash_clear(&spt->page_table, destruct_hash_elem);
  // hash_destroy(&spt->page_table, destruct_hash_elem);
  //  모든 자원 해제하라네? 뭐할까요? 일단 page, elem,

  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
  // TODO: file-backed일 때 dirty 시 쓰기 필요
}
