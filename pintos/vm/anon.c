/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include <string.h>

#include "devices/disk.h"
#include "vm/vm.h"
#include "threads/vaddr.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;

static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
  /* TODO: Set up the swap_disk. */
  swap_disk = disk_get(1, 1);
}

/* Initialize the file mapping */
// 일단 익명이니 0으로?
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  if (VM_TYPE(type) != VM_ANON) {
    return false;
  }
  /* Set up the handler */
  page->operations = &anon_ops;

  struct anon_page *anon_page = &page->anon;       // 요놈
  memset(anon_page, 0, sizeof(struct anon_page));  // 메타데이터를 초기화
  memset(kva, 0, PGSIZE);  // 익명 페이지랑 매핑된 실제 물리값들 초기화
  page->writable = true;
  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
  // page->uninit.init(page, )
  return page->uninit.page_initializer(page, VM_ANON, kva);
}

/* Swap out the page by writing contents to the swap disk. */
/**
 * 현재 frame 내용을 swap space로 보내기(1페이지 8섹터 기록)
 * PTE 해제 + frame 반환? or 분리?
 * 페이지는 메모리가 아닌 swap slot보관 상태로 전환
 */
static bool anon_swap_out(struct page *page) {
  struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  struct anon_page *anon_page = &page->anon;
}