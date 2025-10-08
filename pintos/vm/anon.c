/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include <string.h>

#include "devices/disk.h"
#include "vm/vm.h"
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "threads/synch.h"

#define SECTORS_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_bitmap;
static struct lock swap_lock;

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
  disk_sector_t total_sectors = disk_size(swap_disk);
  size_t total_slots = total_sectors / SECTORS_PER_PAGE;
  swap_bitmap = bitmap_create(total_slots);
  lock_init(&swap_lock);
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
  anon_page->slot_idx = SIZE_MAX;
  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
  disk_sector_t sector_no = NULL;
  struct frame* f = NULL;
  size_t slot_idx = anon_page->slot_idx;

  lock_acquire(&swap_lock);
  if (slot_idx == SIZE_MAX || !bitmap_test(swap_bitmap, slot_idx)) return false;

  for (int i = 0; i < SECTORS_PER_PAGE; i++){
    sector_no = (slot_idx * SECTORS_PER_PAGE) + i;
    disk_read(swap_disk, sector_no, kva + (i * DISK_SECTOR_SIZE));
  }
  bitmap_set(swap_bitmap, slot_idx, false);
  lock_release(&swap_lock);

  anon_page->slot_idx = SIZE_MAX;
  pml4_set_page(page->frame->owner_pml4, page->va, kva, true);
  return true;
}

/* Swap out the page by writing contents to the swap disk. */
/**
 * 현재 frame 내용을 swap space로 보내기(1페이지 8섹터 기록)
 * PTE 해제 + frame 반환? or 분리?
 * 페이지는 메모리가 아닌 swap slot보관 상태로 전환
 */
static bool anon_swap_out(struct page *page) {
  struct anon_page *anon_page = &page->anon;
  void *kva = page->frame->kva;
  lock_acquire(&swap_lock);
  size_t slot_idx = bitmap_scan_and_flip(swap_bitmap, 0, 1, false); // false 는 빈거
  if (slot_idx == BITMAP_ERROR) return false;

  for (int i = 0; i < SECTORS_PER_PAGE; i++){
    disk_sector_t sector = (slot_idx * SECTORS_PER_PAGE) + i;
    disk_write(swap_disk, sector, kva + (i * DISK_SECTOR_SIZE));
  }
  lock_release(&swap_lock);

  anon_page->slot_idx = -1;
  pml4_clear_page(page->frame, page->va);
  page->frame = NULL;
  //free(page->frame); // 이거 제거하면 evict에서 frame이 사라짐 frame해체는 이거 호출하는 쪽으로 ㄱ
  return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  struct anon_page *anon_page = &page->anon;
  lock_acquire(&swap_lock);
  if (anon_page->slot_idx != SIZE_MAX) {
    bitmap_set(swap_bitmap, anon_page->slot_idx, false);
  }
  lock_release(&swap_lock);
  // free(page->frame);
  pml4_clear_page(page->frame->owner_pml4, page->va);
}