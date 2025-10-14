#ifndef VM_VM_H
#define VM_VM_H
// #include <hash.h>
#include <stdbool.h>

#include "lib/kernel/hash.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"

enum vm_type {
  /* page not initialized */
  VM_UNINIT = 0,
  /* page not related to the file, aka anonymous page */
  VM_ANON = 1,
  /* page that realated to the file */
  VM_FILE = 2,
  /* page that hold the page cache, for project 4 */
  VM_PAGE_CACHE = 3,

  /* Bit flags to store state */

  /* Auxillary bit flag marker for store information. You can add more
   * markers, until the value is fit in the int. */
  VM_MARKER_0 = (1 << 3),
  VM_MARKER_1 = (1 << 4),

  /* DO NOT EXCEED THIS VALUE. */
  VM_MARKER_END = (1 << 31),
};

#include "vm/anon.h"
#include "vm/file.h"
#include "vm/uninit.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* "page"의 표현 구조체.
 * 이것은 일종의 "부모 클래스" 역할을 하며,
 * 네 가지 "자식 클래스"(uninit_page, file_page, anon_page,
 * page_cache(project4))를 가진다. 이 구조체에 정의된 기본 멤버는 절대
 * 삭제하거나 수정하지 말 것. */
struct page {
  const struct page_operations *operations;
  void *va;            /* 사용자 공간 기준의 가상 주소 */
  struct frame *frame; /* 해당 물리 프레임을 가리키는 역참조 */

  /* 구현해야 할 부분 */
  struct hash_elem hash_elem;  // hash 소속elem
  bool writable;
  /* 타입별 데이터는 union에 묶여 있다.
   * 각 함수는 현재 union 타입을 자동으로 감지한다. */

  union {
    struct uninit_page uninit;
    struct anon_page anon;
    struct file_page file;
#ifdef EFILESYS
    struct page_cache page_cache;
#endif
  };

  struct mmap_info *mmap_info;
  size_t mmap_page_index;
};

/* The representation of "frame" */
// 메타데이터 구조체 (관리용)
struct frame {
  void *kva;
  struct page *page;
  struct list_elem elem;
};

/* 페이지 동작을 정의한 함수 테이블.
 * C에서 "인터페이스"를 구현하는 한 가지 방식이다.
 * 구조체의 멤버로 "메서드" 테이블을 넣어두고,
 * 필요할 때마다 그 함수를 호출하는 식으로 사용한다. */
struct page_operations {
  bool (*swap_in)(struct page *, void *); /* 스왑 영역 → 메모리 적재 */
  bool (*swap_out)(struct page *);        /* 메모리 → 스왑 영역 저장 */
  void (*destroy)(struct page *);         /* 페이지 소멸 처리 */
  enum vm_type type; /* 페이지 타입 (anon/file/uninit 등) */
};

#define swap_in(page, v) (page)->operations->swap_in((page), v)
#define swap_out(page) (page)->operations->swap_out(page)
#define page_destroy(page) \
  if ((page)->operations->destroy) (page)->operations->destroy(page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
struct supplemental_page_table {
  struct hash page_table;
};

#include "threads/thread.h"
// ✅
void supplemental_page_table_init(struct supplemental_page_table *spt);
// ❌
bool supplemental_page_table_copy(struct supplemental_page_table *dst,
                                  struct supplemental_page_table *src);
// ✅
void supplemental_page_table_kill(struct supplemental_page_table *spt);
// ✅
struct page *spt_find_page(struct supplemental_page_table *spt, void *va);
// ✅
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page);
// ✅
void spt_remove_page(struct supplemental_page_table *spt, struct page *page);

// ❌
void vm_init(void);
// ❌
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
                         bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
  vm_alloc_page_with_initializer((type), (upage), (writable), NULL, NULL)

// ❌
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux);
// ❌
void vm_dealloc_page(struct page *page);
// ❌
bool vm_claim_page(void *va);
// ❌
enum vm_type page_get_type(struct page *page);

#endif
