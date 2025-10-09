/* Hash table.

   This data structure is thoroughly documented in the Tour of
   Pintos for Project 3.

   See hash.h for basic information. */

#include "hash.h"

#include "../debug.h"
#include "include/vm/vm.h"
#include "threads/malloc.h"

#define list_elem_to_hash_elem(LIST_ELEM) \
  list_entry(LIST_ELEM, struct hash_elem, list_elem)

static struct list *find_bucket(struct hash *, struct hash_elem *);
static struct hash_elem *find_elem(struct hash *, struct list *,
                                   struct hash_elem *);
static void insert_elem(struct hash *, struct list *, struct hash_elem *);
static void remove_elem(struct hash *, struct hash_elem *);
static void rehash(struct hash *);

/* Initializes hash table H to compute hash values using HASH and
   compare hash elements using LESS, given auxiliary data AUX. */
bool hash_init(struct hash *h, hash_hash_func *hash, hash_less_func *less,
               void *aux) {
  h->elem_cnt = 0;
  h->bucket_cnt = 4;
  h->buckets = malloc(sizeof *h->buckets * h->bucket_cnt);
  h->hash = hash;
  h->less = less;
  h->aux = aux;

  if (h->buckets != NULL) {
    hash_clear(h, NULL);
    return true;
  } else
    return false;
}

/* 해시 테이블 H의 모든 요소를 제거한다.

   만약 DESTRUCTOR가 null이 아니면, 해시 테이블의 각 요소마다
   DESTRUCTOR가 호출된다.
   DESTRUCTOR는 필요하다면 해시 요소가 사용한 메모리를
   해제(deallocate)할 수도 있다.

   그러나 hash_clear()가 실행되는 동안 해시 테이블 H를 수정하면
   (hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 같은 함수를 사용하는 경우),
   그 행위가 DESTRUCTOR 내부이든 외부이든 상관없이
   동작은 정의되지 않는다(undefined behavior). */
void hash_clear(struct hash *h, hash_action_func *destructor) {
  size_t i;

  for (i = 0; i < h->bucket_cnt; i++) {
    struct list *bucket = &h->buckets[i];

    if (destructor != NULL)
      while (!list_empty(bucket)) {
        struct list_elem *list_elem = list_pop_front(bucket);
        struct hash_elem *hash_elem = list_elem_to_hash_elem(list_elem);
        destructor(hash_elem, h->aux);
      }

    list_init(bucket);
  }

  h->elem_cnt = 0;
}

/* 해시 테이블 H를 파괴(destroy)한다.

   만약 DESTRUCTOR가 null이 아니면,
   먼저 해시 테이블의 각 요소마다 DESTRUCTOR가 호출된다.
   DESTRUCTOR는 필요하다면 해시 요소가 사용한 메모리를
   해제(deallocate)할 수도 있다.

   그러나 hash_clear()가 실행되는 동안 해시 테이블 H를 수정하면
   (hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 같은 함수를 사용하는 경우),
   그 행위가 DESTRUCTOR 내부이든 외부이든 상관없이
   동작은 정의되지 않는다(undefined behavior). */
void hash_destroy(struct hash *h, hash_action_func *destructor) {
  if (destructor != NULL) hash_clear(h, destructor);
  free(h->buckets);
}

/* Inserts NEW into hash table H and returns a null pointer, if
   no equal element is already in the table.
   If an equal element is already in the table, returns it
   without inserting NEW. */
struct hash_elem *hash_insert(struct hash *h, struct hash_elem *new) {
  struct list *bucket = find_bucket(h, new);
  struct hash_elem *old = find_elem(h, bucket, new);

  if (old == NULL) insert_elem(h, bucket, new);

  rehash(h);

  return old;
}

/* Inserts NEW into hash table H, replacing any equal element
   already in the table, which is returned. */
struct hash_elem *hash_replace(struct hash *h, struct hash_elem *new) {
  struct list *bucket = find_bucket(h, new);
  struct hash_elem *old = find_elem(h, bucket, new);

  if (old != NULL) remove_elem(h, old);
  insert_elem(h, bucket, new);

  rehash(h);

  return old;
}

/* Finds and returns an element equal to E in hash table H, or a
   null pointer if no equal element exists in the table. */
struct hash_elem *hash_find(struct hash *h, struct hash_elem *e) {
  return find_elem(h, find_bucket(h, e), e);
}

/* Finds, removes, and returns an element equal to E in hash
   table H.  Returns a null pointer if no equal element existed
   in the table.

   If the elements of the hash table are dynamically allocated,
   or own resources that are, then it is the caller's
   responsibility to deallocate them. */
struct hash_elem *hash_delete(struct hash *h, struct hash_elem *e) {
  struct hash_elem *found = find_elem(h, find_bucket(h, e), e);
  if (found != NULL) {
    remove_elem(h, found);
    rehash(h);
  }
  return found;
}

/* 해시 테이블 H의 각 요소에 대해, 임의의 순서로 ACTION을 호출한다.
   hash_apply()가 실행되는 동안 해시 테이블 H를 수정하면
   (hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 같은 함수를 사용하는 경우),
   그 행위가 ACTION 내부에서든 다른 곳에서든 간에
   동작은 정의되지 않는다(undefined behavior). */
void hash_apply(struct hash *h, hash_action_func *action) {
  size_t i;

  ASSERT(action != NULL);

  for (i = 0; i < h->bucket_cnt; i++) {
    struct list *bucket = &h->buckets[i];
    struct list_elem *elem, *next;

    for (elem = list_begin(bucket); elem != list_end(bucket); elem = next) {
      next = list_next(elem);
      action(list_elem_to_hash_elem(elem), h->aux);
    }
  }
}

/* Initializes I for iterating hash table H.

   Iteration idiom:

   struct hash_iterator i;

   hash_first (&i, h);
   while (hash_next (&i))
   {
   struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);
   ...do something with f...
   }

   Modifying hash table H during iteration, using any of the
   functions hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), or hash_delete(), invalidates all
   iterators. */
void hash_first(struct hash_iterator *i, struct hash *h) {
  ASSERT(i != NULL);
  ASSERT(h != NULL);

  i->hash = h;
  i->bucket = i->hash->buckets;
  i->elem = list_elem_to_hash_elem(list_head(i->bucket));
}

/* 반복자 I를 해시 테이블의 다음 요소로 이동시키고, 그 요소를 반환한다.
   더 이상 요소가 없으면 null 포인터를 반환한다.
   요소들은 임의의 순서로 반환된다.

   해시 테이블 H를 순회(iteration)하는 도중에
   hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 같은 함수를 호출하여
   테이블을 수정하면, 모든 iterators는 무효화된다. */
struct hash_elem *hash_next(struct hash_iterator *i) {
  ASSERT(i != NULL);

  i->elem = list_elem_to_hash_elem(list_next(&i->elem->list_elem));
  while (i->elem == list_elem_to_hash_elem(list_end(i->bucket))) {
    if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt) {
      i->elem = NULL;
      break;
    }
    i->elem = list_elem_to_hash_elem(list_begin(i->bucket));
  }

  return i->elem;
}

/* Returns the current element in the hash table iteration, or a
   null pointer at the end of the table.  Undefined behavior
   after calling hash_first() but before hash_next(). */
struct hash_elem *hash_cur(struct hash_iterator *i) { return i->elem; }

/* Returns the number of elements in H. */
size_t hash_size(struct hash *h) { return h->elem_cnt; }

/* Returns true if H contains no elements, false otherwise. */
bool hash_empty(struct hash *h) { return h->elem_cnt == 0; }

/* Fowler-Noll-Vo hash constants, for 32-bit word sizes. */
#define FNV_64_PRIME 0x00000100000001B3UL
#define FNV_64_BASIS 0xcbf29ce484222325UL

/* Returns a hash of the SIZE bytes in BUF. */
uint64_t hash_bytes(const void *buf_, size_t size) {
  /* Fowler-Noll-Vo 32-bit hash, for bytes. */
  const unsigned char *buf = buf_;
  uint64_t hash;

  ASSERT(buf != NULL);

  hash = FNV_64_BASIS;
  while (size-- > 0) hash = (hash * FNV_64_PRIME) ^ *buf++;

  return hash;
}

/* Returns a hash of string S. */
uint64_t hash_string(const char *s_) {
  const unsigned char *s = (const unsigned char *)s_;
  uint64_t hash;

  ASSERT(s != NULL);

  hash = FNV_64_BASIS;
  while (*s != '\0') hash = (hash * FNV_64_PRIME) ^ *s++;

  return hash;
}

/* Returns a hash of integer I. */
uint64_t hash_int(int i) { return hash_bytes(&i, sizeof i); }

/* Returns the bucket in H that E belongs in. */
static struct list *find_bucket(struct hash *h, struct hash_elem *e) {
  size_t bucket_idx = h->hash(e, h->aux) & (h->bucket_cnt - 1);
  return &h->buckets[bucket_idx];
}

/* Searches BUCKET in H for a hash element equal to E.  Returns
   it if found or a null pointer otherwise. */
static struct hash_elem *find_elem(struct hash *h, struct list *bucket,
                                   struct hash_elem *e) {
  struct list_elem *i;

  for (i = list_begin(bucket); i != list_end(bucket); i = list_next(i)) {
    struct hash_elem *hi = list_elem_to_hash_elem(i);
    if (!h->less(hi, e, h->aux) && !h->less(e, hi, h->aux)) return hi;
  }  // 크거나 같으면 false 반대로도 크거나 같으면 false >> 반환
  return NULL;
}

/* Returns X with its lowest-order bit set to 1 turned off. */
static inline size_t turn_off_least_1bit(size_t x) { return x & (x - 1); }

/* Returns true if X is a power of 2, otherwise false. */
static inline size_t is_power_of_2(size_t x) {
  return x != 0 && turn_off_least_1bit(x) == 0;
}

/* Element per bucket ratios. */
#define MIN_ELEMS_PER_BUCKET 1  /* Elems/bucket < 1: reduce # of buckets. */
#define BEST_ELEMS_PER_BUCKET 2 /* Ideal elems/bucket. */
#define MAX_ELEMS_PER_BUCKET 4  /* Elems/bucket > 4: increase # of buckets. */

/* 해시 테이블 H의 버킷 개수를 이상적인 값으로 변경한다.
   이 함수는 메모리 부족(out-of-memory) 상황 때문에 실패할 수 있다.
   하지만 그 경우 해시 접근이 다소 비효율적일 뿐,
   여전히 계속 사용할 수는 있다. */
static void rehash(struct hash *h) {
  size_t old_bucket_cnt, new_bucket_cnt;
  struct list *new_buckets, *old_buckets;
  size_t i;

  ASSERT(h != NULL);

  /* 나중에 사용할 수 있도록 기존 버킷 정보를 저장한다. */
  old_buckets = h->buckets;
  old_bucket_cnt = h->bucket_cnt;

  /* 이번에 사용할 버킷 개수를 계산한다.
     각 BEST_ELEMS_PER_BUCKET 당 하나의 버킷을 갖도록 하고자 한다.
     최소한 4개의 버킷은 있어야 하며,
     버킷 개수는 반드시 2의 거듭제곱이어야 한다. */
  new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;
  if (new_bucket_cnt < 4) new_bucket_cnt = 4;
  while (!is_power_of_2(new_bucket_cnt))
    new_bucket_cnt = turn_off_least_1bit(new_bucket_cnt);

  /* 버킷 개수가 변하지 않는다면 아무것도 하지 않는다. */
  if (new_bucket_cnt == old_bucket_cnt) return;

  /* 새 버킷들을 할당하고 비어 있는 상태로 초기화한다. */
  new_buckets = malloc(sizeof *new_buckets * new_bucket_cnt);
  if (new_buckets == NULL) {
    /* 메모리 할당 실패.
       이 경우 해시 테이블 사용은 다소 비효율적이 된다.
       그러나 여전히 사용 가능하므로,
       오류로 처리할 이유는 없다. */
    return;
  }
  for (i = 0; i < new_bucket_cnt; i++) list_init(&new_buckets[i]);

  /* 새로운 버킷 정보를 적용한다. */
  h->buckets = new_buckets;
  h->bucket_cnt = new_bucket_cnt;

  /* 각 기존 요소들을 적절한 새로운 버킷으로 이동시킨다. */
  for (i = 0; i < old_bucket_cnt; i++) {
    struct list *old_bucket;
    struct list_elem *elem, *next;

    old_bucket = &old_buckets[i];
    for (elem = list_begin(old_bucket); elem != list_end(old_bucket);
         elem = next) {
      struct list *new_bucket = find_bucket(h, list_elem_to_hash_elem(elem));
      next = list_next(elem);
      list_remove(elem);
      list_push_front(new_bucket, elem);
    }
  }

  free(old_buckets);
}

/* Inserts E into BUCKET (in hash table H). */
static void insert_elem(struct hash *h, struct list *bucket,
                        struct hash_elem *e) {
  h->elem_cnt++;
  list_push_front(bucket, &e->list_elem);
}

/* Removes E from hash table H. */
static void remove_elem(struct hash *h, struct hash_elem *e) {
  h->elem_cnt--;
  list_remove(&e->list_elem);
}

/* hash_elem을 va로 비교하는함수 */
bool page_less(const struct hash_elem *a, const struct hash_elem *b,
               void *aux) {
  // ASSERT(a != NULL);
  // ASSERT(b != NULL);

  struct page *page1 = hash_entry(a, struct page, hash_elem);
  struct page *page2 = hash_entry(b, struct page, hash_elem);
  return page1->va < page2->va ? true : false;
  /* A가 B보다 작으면 true를 반환하고,
   * A가 B보다 크거나 같으면 false를 반환한다. */
}

/* 해시 요소 E에 대해 어떤 연산을 수행한다.
 * 이때 보조 데이터 AUX를 함께 사용한다. */
typedef void hash_action_func(struct hash_elem *e, void *aux);

// GitBook 曰 : pml4,랑 palloc된 메모리(frame)는 신경쓰지 마라 (호출자가 알아서
// 제거할 것 part supplemental_page_table_kill)
void destruct_hash_elem(struct hash_elem *e, void *aux) {
  ASSERT(e != NULL);
  struct page *page = hash_entry(e, struct page, hash_elem);
  vm_dealloc_page(page);  // 메크로 사용 깔끔
  // 아래는 어차피 destroy에서 하게 놨두는게 좋나????
  // if (frame != NULL) {
  //   dealloc_frame(frame);
  // }
}
/* 해시 요소 E에 대해 해시 값을 계산하여 반환한다.
 * 이때 보조 데이터 AUX를 함께 사용한다. */
uint64_t page_hash(const struct hash_elem *e, void *aux) {
  struct page *p = hash_entry(e, struct page, hash_elem);
  return hash_bytes(&p->va, sizeof p->va);
}