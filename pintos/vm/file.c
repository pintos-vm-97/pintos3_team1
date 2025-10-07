/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/malloc.h"

static bool file_backed_swap_in(struct page* page, void* kva);
static bool file_backed_swap_out(struct page* page);
static void file_backed_destroy(struct page* page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void) {
  // list_init(&thread_current()->mmap_list);
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page* page, enum vm_type type, void* kva) {
  /* Set up the handler */
  page->operations = &file_ops;
  struct file_page* file_page = &page->file;
  struct lazy_load_aux* aux = page->uninit.aux;

  file_page->file = aux->file;
  file_page->ofs = aux->ofs;
  file_page->page_read_bytes = aux->page_read_bytes;
  file_page->page_zero_bytes = aux->page_zero_bytes;
  file_page->is_writable = aux->is_writable;
  return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page* page, void* kva) {
  struct file_page* file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
/**
 * Dirty 확인 필요
 * - Dirty 시 disk로 쓰기 작업 必
 *
 * spt에 elem 제거 必
 */
static bool file_backed_swap_out(struct page* page) {
  struct file_page* file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page* page) {
  // dirty여부 파악하고
  struct file_page* file_page UNUSED = &page->file;
  free(page->frame);
  if (pml4_is_dirty(thread_current()->pml4, page->va)) {
  }
  // file_close(file_page->file);
  pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
// stick out -> 페이지 단위 작업시 페이지 끝 튀어나오는 부분 0으로 처리 필
void* do_mmap(void* addr, size_t length, int writable, struct file* file,
              off_t offset) {
  ASSERT(addr != 0 || addr != NULL || length > 0 || pg_ofs(addr) == 0);

  struct lazy_load_aux* aux = NULL;
  off_t f_length = file_length(file);

  size_t read_bytes = (f_length > length) ? length : f_length;
  size_t total_zero_bytes = PGSIZE - (read_bytes % PGSIZE);
  int need_page_cnt = (read_bytes + PGSIZE - 1) / PGSIZE;
  void* base_addr = pg_round_down(addr);
  void* upage = base_addr;

  struct file* reopened_file = file_reopen(file);
  if (reopened_file == NULL) return NULL;

  while (read_bytes > 0 || total_zero_bytes > 0) {
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    upage = pg_round_down(upage);

    /* TODO: Set up aux to pass information to the lazy_load_segment. */
    aux = malloc(sizeof(struct lazy_load_aux));
    if (aux == NULL) return NULL;
    aux->file = reopened_file;
    aux->total_file_length = f_length;
    aux->page_read_bytes = page_read_bytes;
    aux->ofs = offset;
    aux->is_writable = writable;
    aux->page_zero_bytes = page_zero_bytes;
    aux->is_reopened = true;
    if (!vm_alloc_page_with_initializer(VM_FILE, upage, writable,
                                        lazy_load_segment, aux)) {
      goto err_cleanup;
    }

    struct page* page = spt_find_page(&thread_current()->spt, upage);
    if (page == NULL) goto err_cleanup;

    struct mmap_region* region = malloc(sizeof(struct mmap_region));
    if (region == NULL) goto err_cleanup;
    page->mmap_region = region;
    region->file = aux->file;
    region->base_addr = base_addr;
    region->total_mapped_cnt = need_page_cnt;
    region->addr = upage;
    list_push_back(&thread_current()->mmap_list, &region->elem);

    read_bytes -= page_read_bytes;
    total_zero_bytes -= page_zero_bytes;
    offset += page_read_bytes;
    upage += PGSIZE;
  }
  return addr;

err_cleanup:
  free(aux);
  do_munmap(addr);
  return NULL;
}

static void remove_related_regions(void* base_addr) {
  struct thread* t = thread_current();
  struct list* mmap_list = &t->mmap_list;
  struct list_elem* e = list_begin(mmap_list);

  while (e != list_end(mmap_list)) {
    struct list_elem* next = list_next(e);
    struct mmap_region* region = list_entry(e, struct mmap_region, elem);

    if (region != NULL && region->base_addr == base_addr) {
      struct page* page = spt_find_page(&t->spt, pg_round_down(region->addr));
      if (page != NULL) {
        spt_remove_page(&t->spt, page);
      }
      list_remove(e);
      free(region);
    }
    e = next;
  }
}
/* Do the munmap */
/* 풀어야 할 점 : addr이 속한 page 말고도 추가 page도 unmap해야할텐데... */
// mmap리스트를 가져야되긴 하는데.. Cuz process exit시 해당 스레드가 가진
// mmap들을 전부 munmap 해야 됨
void do_munmap(void* addr) {
  if (addr == NULL) return;

  struct thread* t = thread_current();
  struct list* mmap_list = &t->mmap_list;
  struct mmap_region* region = NULL;
  struct list_elem* e;
  struct page* page = NULL;
  struct file* file = NULL;

  if (list_empty(mmap_list)) return;
  //printf("munmap addr : %p\n", addr);


  page = spt_find_page(&t->spt, pg_round_down(addr));
  if (page == NULL || page->mmap_region == NULL) return;

  region = page->mmap_region;
  file = region->file;

  remove_related_regions(region->base_addr);
  file_close(file);
}
