#ifndef __LIB_KERNEL_HASH_H
#define __LIB_KERNEL_HASH_H
/* 해시 테이블.
 *
 * 이 자료구조는 Pintos Project 3의 Tour 문서에 자세히 설명되어 있다.
 *
 * 이것은 체이닝(chaining)을 사용하는 표준 해시 테이블이다.
 * 테이블에서 원소를 찾을 때는, 원소의 데이터를 기반으로 해시 함수를 계산한 뒤
 * 그 값을 배열 인덱스로 사용하여 이중 연결 리스트(doubly linked list)의
 * 어느 체인에 속하는지 찾는다. 이후 리스트를 선형 탐색(linear search)한다.
 *
 * 체인 리스트는 동적 할당을 사용하지 않는다.
 * 대신, 해시에 들어갈 수 있는 모든 구조체는 반드시 `struct hash_elem`
 * 멤버를 포함해야 한다. 모든 해시 함수는 이 `struct hash_elem`을 기반으로
 * 동작한다. `hash_entry` 매크로를 사용하면 `struct hash_elem` 포인터로부터
 * 그것을 포함하고 있는 외부 구조체 객체로 다시 변환할 수 있다.
 * 이 기법은 연결 리스트(list) 구현에서도 동일하게 사용된다.
 * 자세한 내용은 lib/kernel/list.h를 참고할 것.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "list.h"

/* 구분자 + 확장가능성 */
struct hash_elem {
  struct list_elem list_elem;
};

/* 해시 요소(hash element)를 가리키는 포인터 HASH_ELEM을,
 * 그 hash element를 포함하고 있는 외부 구조체의 포인터로 변환한다.
 * STRUCT에는 외부 구조체의 이름을,
 * MEMBER에는 hash element의 멤버 이름을 넣어야 한다.
 * 파일 상단의 큰 주석에 예시가 있으니 참고할 것. */
#define hash_entry(HASH_ELEM, STRUCT, MEMBER)      \
  ((STRUCT *)((uint8_t *)&(HASH_ELEM)->list_elem - \
              offsetof(STRUCT, MEMBER.list_elem)))

/* 해시 요소 E에 대해 해시 값을 계산하여 반환한다.
 * 이때 보조 데이터 AUX를 함께 사용한다. */
typedef uint64_t hash_hash_func(const struct hash_elem *e, void *aux);

/* 두 해시 요소 A와 B의 값을 비교한다.
 * 보조 데이터 AUX를 함께 사용한다.
 * A가 B보다 작으면 true를 반환하고,
 * A가 B보다 크거나 같으면 false를 반환한다. */
typedef bool hash_less_func(const struct hash_elem *a,
                            const struct hash_elem *b, void *aux);
/* 해시 요소 E에 대해 어떤 연산을 수행한다.
 * 이때 보조 데이터 AUX를 함께 사용한다. */
typedef void hash_action_func(struct hash_elem *e, void *aux);

/* 해시 테이블. */
struct hash {
  size_t elem_cnt;      /* 테이블에 들어 있는 요소 개수. */
  size_t bucket_cnt;    /* 버킷 개수 (2의 거듭제곱이어야 함). */
  struct list *buckets; /* bucket_cnt 크기의 리스트 배열. */
  hash_hash_func *hash; /* 해시 함수 포인터. */
  hash_less_func *less; /* 비교 함수 포인터. */
  void *aux;            /* hash/less 함수에 전달할 보조 데이터. */
};

/* 해시 테이블 반복자(iterator). */
struct hash_iterator {
  struct hash *hash;      /* 현재 순회 중인 해시 테이블. */
  struct list *bucket;    /* 현재 가리키는 버킷. */
  struct hash_elem *elem; /* 현재 버킷 내의 해시 요소. */
};

/* Basic life cycle. */
/* hash_less 구현. */
bool hash_init(struct hash *, hash_hash_func *, hash_less_func *, void *aux);
void hash_clear(struct hash *, hash_action_func *);
void hash_destroy(struct hash *, hash_action_func *);

/* Search, insertion, deletion. */
struct hash_elem *hash_insert(struct hash *, struct hash_elem *);
struct hash_elem *hash_replace(struct hash *, struct hash_elem *);
struct hash_elem *hash_find(struct hash *, struct hash_elem *);
struct hash_elem *hash_delete(struct hash *, struct hash_elem *);

/* Iteration. */
void hash_apply(struct hash *, hash_action_func *);
void hash_first(struct hash_iterator *, struct hash *);
struct hash_elem *hash_next(struct hash_iterator *);
struct hash_elem *hash_cur(struct hash_iterator *);

/* Information. */
size_t hash_size(struct hash *);
bool hash_empty(struct hash *);

/* Sample hash functions. */
uint64_t hash_bytes(const void *, size_t);
uint64_t hash_string(const char *);
uint64_t hash_int(int);

bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);
void destruct_hash_elem(struct hash_elem *e, void *aux);
uint64_t page_hash(const struct hash_elem *e, void *aux);
#endif /* lib/kernel/hash.h */
