/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

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
void vm_file_init(void) {}

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
}

/* Do the mmap */
void* do_mmap(void* addr, size_t length, int writable, struct file* file,
              off_t offset) {

  size_t read_bytes = 0;
  void *upage = addr;

  while (1) {
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    upage = pg_round_down(addr);

    /* TODO: Set up aux to pass information to the lazy_load_segment. */
    struct lazy_load_aux* aux = malloc(sizeof(struct lazy_load_aux));
    aux->file = file;
    aux->page_read_bytes = page_read_bytes;
    aux->ofs = offset;
    aux->is_writable = writable;
    if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable,
                                        lazy_load_segment, aux)) {
      free(aux);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    offset += page_read_bytes;
    upage += PGSIZE;
  }
  return NULL;
}

/* Do the munmap */
void do_munmap(void* addr) {
  struct page* page = spt_find_page(&thread_current()->spt, addr);

  file_backed_destroy(page);
  
}
