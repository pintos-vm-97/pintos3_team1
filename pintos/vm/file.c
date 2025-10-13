/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void) {}

/* 📌 Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &file_ops;

  struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
  struct file_page *file_page = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
  struct file_page *file_page = &page->file;
}

/* 📌 Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
  struct file_page *file_page = &page->file;
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset) {
  // 반환할 주소
  void *start_addr = addr;
  // 파일은 새로 열어야 함
  struct file *reopened = file_reopen(file);
  if (reopened == NULL) return NULL;

  // 페이지마다 매핑
  while (length > 0) {
    size_t read_byte = length > PGSIZE ? PGSIZE : length;
    size_t zero_byte = PGSIZE - read_byte;

    struct lazy_load_aux *aux = malloc(sizeof(struct lazy_load_aux));
    if (aux == NULL) return NULL;
    aux->file = reopened;
    aux->page_read_bytes = read_byte;
    aux->page_zero_bytes = zero_byte;
    aux->ofs = offset;
    if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable,
                                        lazy_load_segment, aux)) {
      free(aux);
      do_munmap(start_addr);
      return NULL;
    }

    /* Advance. */
    length -= read_byte;
    offset += read_byte;
    addr += PGSIZE;
  }

  return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr) {}
