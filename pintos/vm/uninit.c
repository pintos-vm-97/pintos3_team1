/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/uninit.h"


#include "vm/vm.h"

static bool uninit_initialize(struct page *page, void *kva);
static void uninit_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
    .swap_in = uninit_initialize,
    .swap_out = NULL,
    .destroy = uninit_destroy,
    .type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void uninit_new(struct page *page, void *va, vm_initializer *init,
                enum vm_type type, void *aux,
                bool (*initializer)(struct page *, enum vm_type, void *)) {
  ASSERT(page != NULL);

  *page = (struct page){
      .operations = &uninit_ops,  // 일단 uninit 전용 operatoins 연결
      .va = va,                   // 페이지가 담당할 사용자 가상주소
      .frame = NULL,              // 실제 프레임은 NULL
      .uninit = (struct uninit_page){
          .init =
              init,  // lazy-load 때 실제 내용을 채울 함수(vm_initializer같은..)
          .type = type,  // 최종 타입힌트 (anon, file)
          .aux = aux,    // init에 전달할 부가정보
          .page_initializer =
              initializer,  // uninit->실제타입으로 변환시키는 함수
      }};
}

/* Initalize the page on first fault */
static bool uninit_initialize(struct page *page, void *kva) {
  struct uninit_page *uninit = &page->uninit;

  /* Fetch first, page_initialize may overwrite the values */
  vm_initializer *init = uninit->init;  // 내용채우기 함수 포인터 백업
  void *aux = uninit->aux;              // 부가정보 백업
  // 이것을 복사하는이유는 안하면 위험. 위험한 이유는 모름
  /* TODO: You may need to fix this function. */
  return uninit->page_initializer(page, uninit->type, kva) &&
         (init ? init(page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void uninit_destroy(struct page *page) {
  struct uninit_page *uninit UNUSED = &page->uninit;
  /* TODO: Fill this function.
   * TODO: If you don't have anything to do, just return. */
}

