#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdbool.h>
#include <stdint.h>

#include "threads/loader.h"

/* Functions and macros for working with virtual addresses.
 *
 * See pte.h for functions and macros specifically for x86
 * hardware page tables. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* 페이지 내부 오프셋 관련 상수 */
#define PGSHIFT 0
#define PGBITS 12
#define PGSIZE (1 << PGBITS)
#define PGMASK BITMASK(PGSHIFT, PGBITS)

/* VA(가상 주소)에서 페이지 내부 오프셋을 얻는다. (0 ~ 4095 범위 값) */
#define pg_ofs(va) ((uint64_t)(va) & PGMASK)

/* VA가 속한 페이지 번호를 얻는다. (VA >> 12) */
#define pg_no(va) ((uint64_t)(va) >> PGBITS)

/* VA를 올림 처리하여 가장 가까운 페이지 경계(상위 주소)로 맞춘다. */
#define pg_round_up(va) ((void *)(((uint64_t)(va) + PGSIZE - 1) & ~PGMASK))

/* VA를 내림 처리하여 가장 가까운 페이지 경계(하위 주소)로 맞춘다. */
#define pg_round_down(va) (void *)((uint64_t)(va) & ~PGMASK)

/* Kernel virtual address start */
#define KERN_BASE LOADER_KERN_BASE

/* User stack start */
#define USER_STACK 0x47480000

/* Returns true if VADDR is a user virtual address. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* KERN_BASE 이상이면 커널 가상주소로 간주 */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: add checking
/* Returns kernel virtual address at which physical address PADDR
 *  is mapped. */
#define ptov(paddr) ((void *)(((uint64_t)paddr) + KERN_BASE))

/* Returns physical address at which kernel virtual address VADDR
 * is mapped. */
#define vtop(vaddr)                            \
  ({                                           \
    ASSERT(is_kernel_vaddr(vaddr));            \
    ((uint64_t)(vaddr) - (uint64_t)KERN_BASE); \
  })

#endif /* threads/vaddr.h */
