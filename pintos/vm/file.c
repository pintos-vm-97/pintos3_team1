/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);
static struct mmap_info *find_mminfo_by_addr(struct thread *t, void *addr);
static bool lazy_file_segment(struct page *page, void *aux);
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
  file_page->fp_aux = NULL;
  return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
  struct file_page *file_page = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
  struct file_page *file_page = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
  struct file_page *file_page = &page->file;
  struct lazy_load_aux *file_info = file_page->fp_aux;
  struct thread *cur = thread_current();

  if (file_info) {
    // dirty(수정된) 페이지는 파일에 저장해야함
    if (pml4_is_dirty(cur->pml4, page->va))
      file_write_at(file_info->file, page->frame->kva,
                    file_info->page_read_bytes, file_info->ofs);

    // 더 이상 file_info 쓰지 않으면 free
    // file_close(file_info->file);
    free(file_info);
  }

  // 프레임 해제 // remove
  // free(page->frame);
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset) {
  // 반환할 주소
  void *start_addr = addr;
  // 파일은 새로 열어야 함
  struct file *reopened = file_reopen(file);
  if (reopened == NULL) return NULL;

  struct mmap_info *mp_info = malloc(sizeof(struct mmap_info));
  if (mp_info == NULL) return NULL;

  mp_info->file = reopened;
  mp_info->start_addr = start_addr;
  mp_info->page_cnt = 0;
  list_push_back(&thread_current()->mm_list, &mp_info->elem);

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
                                        lazy_file_segment, aux)) {
      free(aux);
      return NULL;
    }

    struct page *page = spt_find_page(&thread_current()->spt, addr);
    page->mmap_info = mp_info;
    page->mmap_page_index = mp_info->page_cnt;
    mp_info->page_cnt++;
    /* Advance. */
    length -= read_byte;
    offset += read_byte;
    addr += PGSIZE;
  }

  return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr) {
  struct thread *t = thread_current();
  struct page *page = spt_find_page(&t->spt, addr);
  if (page == NULL || page->mmap_info == NULL) return;

  struct mmap_info *mp = page->mmap_info;
  void *cur = mp->start_addr;

  for (int i = 0; i < mp->page_cnt; i++) {
    struct page *p = spt_find_page(&t->spt, cur);
    if (p) spt_remove_page(&t->spt, p);  // → destroy에서 dirty 처리
    cur += PGSIZE;
  }

  file_close(mp->file);
  list_remove(&mp->elem);
  free(mp);
}

static bool lazy_file_segment(struct page *page, void *aux) {
  /* TODO: Load the segment from the file */
  struct lazy_load_aux *p_aux = aux;

  /* TODO: This called when the first page fault occurs on address VA. */

  void *p_kva = page->frame->kva;  // 물리 프레임의 커널 주소

  size_t page_read_byte = p_aux->page_read_bytes;  // 읽을 바이트 수
  size_t page_zero_byte = p_aux->page_zero_bytes;  // 제로 바이트 수

  /* TODO: VA is available when calling this function. */

  if (page_read_byte > 0)
    file_read_at(p_aux->file, p_kva, page_read_byte, p_aux->ofs);

  // 남은 영역 0으로 채우기
  if (page_zero_byte > 0) memset(p_kva + page_read_byte, 0, page_zero_byte);

  // file_info 받아오기
  page->file.fp_aux = p_aux;

  return true;
}