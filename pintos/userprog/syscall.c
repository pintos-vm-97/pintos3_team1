#include "userprog/syscall.h"

#include <debug.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "string.h"

#include "devices/input.h"
#include "filesys/directory.h"  // 디렉터리 관련 자료구조 및 함수 (디렉터리 열기, 탐색 등)
#include "filesys/file.h"  // 개별 파일 객체(file 구조체) 및 파일 입출력 함수 정의 (read, write 등)
#include "filesys/filesys.h"  // 파일 시스템 전반에 대한 함수 및 초기화/포맷 인터페이스
#include "include/threads/init.h"
#include "intrinsic.h"
#include "lib/kernel/console.h"  // 커널 콘솔 입출력 함수 제공 (putbuf, printf 등)
#include "lib/kernel/stdio.h"
#include "lib/user/syscall.h"  // 유저 프로그램이 사용하는 시스템 콜 번호 및 인터페이스 정의
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/process.h"

struct lock filesys_lock;  // 파일 시스템 동기화용 전역 락

#ifndef STDIN_FILENO
#define STDIN_FILENO 0  // 표준 입력 파일 디스크립터 번호
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1  // 표준 출력 파일 디스크립터 번호
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2  // 표준 에러 파일 디스크립터 번호
#endif

static struct file stdin_dummy;   // STDIN을 나타내기 위한 더미 file 구조체
static struct file stdout_dummy;  // STDOUT을 나타내기 위한 더미 file 구조체
static bool is_user_mapped(const void *u_addr);
static bool only_user_addr(const void *u_addr);
static bool check_user_buffer(void *buffer, size_t size, bool to_read);
static void copy_in(void *dst, const void *src, size_t size);
static bool copy_in_string(void *k_addr, const char *user_str);
static void *syscall_mmap(void *addr, size_t length, int writable, int fd,
                          off_t offset);
static void syscall_munmap(void *start_addr);
static int check_fd(struct thread *t, int fd);

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void) {
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG)
                                                               << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

  /* 인터럽트 서비스 루틴(ISR)은 syscall_entry가
   * 사용자 스택을 커널 모드 스택으로 교체하기 전까지는
   * 어떤 인터럽트도 처리하면 안 된다.
   * 따라서 FLAG_FL을 마스킹했다.
   * 시스템 콜 진입 시점에서 아직 사용자 모드 스택이 커널 스택으로 바뀌지
   * 않았으니, 그 사이에 인터럽트가 발생하면 스택이 꼬여서 문제가 생길 수 있음 →
   * 그래서 인터럽트를 잠깐 막아 둔다 */
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

  // syscall_init 맨 밑에 추가
  lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f) {
  switch ((int)f->R.rax)  // 시스템 콜 번호는 intr_frame의 R.rax에 담겨져서 옴
  {
    case SYS_HALT:
      syscall_halt();  // PintOS 종료
      break;
    case SYS_EXIT:
      int status = (int)f->R.rdi;
      syscall_exit(status);
      break;
    case SYS_FORK:
      f->R.rax = syscall_fork((const char *)f->R.rdi, f);
      break;
    case SYS_EXEC:
      f->R.rax = syscall_exec((const char *)f->R.rdi);
      break;
    case SYS_WAIT:
      f->R.rax = syscall_wait((int)f->R.rdi);
      break;
    case SYS_CREATE:
      // 인자 1 : 파일, 인자 2 : 사이즈
      f->R.rax = syscall_create((const char *)f->R.rdi, (unsigned)f->R.rsi);
      break;
    case SYS_REMOVE:
      f->R.rax = syscall_remove((const char *)f->R.rdi);
      break;
    case SYS_OPEN:
      f->R.rax = syscall_open((const char *)f->R.rdi);
      break;
    case SYS_FILESIZE:
      f->R.rax = syscall_filesize((int)f->R.rdi);
      break;
    case SYS_READ:
      f->R.rax = syscall_read((int)f->R.rdi, (const void *)f->R.rsi,
                              (unsigned)f->R.rdx);
      break;
    case SYS_WRITE:
      int fd = (int)f->R.rdi;
      const void *buf = (const void *)f->R.rsi;
      unsigned size = (unsigned)f->R.rdx;
      int bytes = syscall_write(fd, buf, size);
      f->R.rax = bytes;
      break;
    case SYS_SEEK:
      syscall_seek((int)f->R.rdi, (unsigned)f->R.rsi);
      break;
    case SYS_TELL:
      f->R.rax = syscall_tell((int)f->R.rdi);
      break;
    case SYS_CLOSE:
      syscall_close((int)f->R.rdi);
      break;
    case SYS_DUP2:
      // dup2(oldfd, newfd) 요청을 처리하고 반환값을 rax에 기록
      f->R.rax = syscall_dup2((int)f->R.rdi, (int)f->R.rsi);
      break;
    case SYS_MMAP:
      f->R.rax = syscall_mmap((void *)f->R.rdi, (size_t)f->R.rsi, (int)f->R.rdx,
                              (int)f->R.rcx, (off_t)f->R.r8);
      break;

    case SYS_MUNMAP:
      syscall_munmap((void *)f->R.rdi);

      break;
    default:
      printf("system call!\n");
      thread_exit();
      break;
  }
}

struct file *syscall_get_std_file(int fd) {
  switch (fd) {
    case STDIN_FILENO:
      return &stdin_dummy;  // STDIN이 요청되면 STDIN 더미 객체 반환
    case STDOUT_FILENO:
      return &stdout_dummy;  // STDOUT이 요청되면 STDOUT 더미 객체 반환
    default:
      return NULL;  // 그 외 번호는 표준 스트림이 아니므로 NULL 반환
  }
}

int syscall_dup2(int oldfd, int newfd) {
  struct thread *current =
      thread_current();  // 현재 실행 중인 스레드 포인터 확보

  if (current->FDT == NULL) {
    return -1;  // FDT가 없다면 파일 디스크립터를 복제할 수 없음
  }

  if (oldfd == newfd) {
    return newfd;  // 같은 번호로 복제 요청 시 그대로 반환
  }

  if (oldfd < 0 || oldfd >= MAX_FD || newfd < 0 || newfd >= MAX_FD) {
    return -1;  // 허용 범위를 벗어나면 실패 처리
  }

  struct file *old_file =
      process_get_file(oldfd);  // 원본 fd에 연결된 파일 객체 조회

  if (old_file == NULL) {
    return -1;  // 원본 fd가 열려 있지 않다면 복제 불가
  }

  struct file *stdin_file =
      syscall_get_std_file(STDIN_FILENO);  // STDIN 더미 파일 포인터 캐싱
  struct file *stdout_file =
      syscall_get_std_file(STDOUT_FILENO);  // STDOUT 더미 파일 포인터 캐싱

  if (old_file == stdin_file && current->stdin_count == 0) {
    return -1;  // STDIN이 이미 모두 닫힌 상태라면 복제할 수 없음
  }

  if (old_file == stdout_file && current->stdout_count == 0) {
    return -1;  // STDOUT이 이미 모두 닫힌 상태라면 복제할 수 없음
  }

  syscall_close(newfd);  // 대상 fd가 열려 있다면 먼저 닫아서 자리 확보

  if (old_file == stdin_file) {
    current->FDT[newfd] = stdin_file;  // 새 fd가 STDIN을 가리키도록 설정
    current->stdin_count++;            // STDIN 참조 카운트 증가
    return newfd;                      // 복제된 fd 반환
  }

  if (old_file == stdout_file) {
    current->FDT[newfd] = stdout_file;  // 새 fd가 STDOUT을 가리키도록 설정
    current->stdout_count++;            // STDOUT 참조 카운트 증가
    return newfd;                       // 복제된 fd 반환
  }

  lock_acquire(&filesys_lock);  // 일반 파일 공유 시 dup_count 갱신을 보호하기
                                // 위한 락 획득
  old_file->dup_count++;        // 동일 파일을 가리키는 추가 참조를 기록
  lock_release(&filesys_lock);  // dup_count 갱신 후 락 해제

  current->FDT[newfd] = old_file;  // 새 fd가 원래 파일 객체를 공유하도록 설정

  return newfd;  // 복제된 fd 번호 반환
}

unsigned syscall_tell(int fd) {
  // 파일 디스크립터를 통해 파일 객체 가져오기
  struct file *file = process_get_file(fd);

  // 유효하지 않은 fd이면 0 반환 (unsigned 타입이므로 -1 대신 0 사용)
  if (file == NULL) {
    return 0;
  }

  // 현재 파일의 읽기/쓰기 위치(offset)를 반환
  return file_tell(file);
}

void syscall_seek(int fd, unsigned position) {
  // 파일 디스크립터를 통해 파일 객체 가져오기
  struct file *file = process_get_file(fd);

  // 유효하지 않은 fd이면 아무 작업도 하지 않고 종료
  if (file == NULL) {
    return;
  }

  // 파일의 읽기/쓰기 위치를 지정된 위치로 변경
  file_seek(file, position);
}

void syscall_close(int fd) {
  if (fd < 0 || fd >= MAX_FD) {
    return;  // 허용 범위를 벗어난 fd는 무시
  }

  struct thread *current = thread_current();  // 현재 스레드 포인터 획득
  if (current->FDT == NULL) {
    return;  // FDT가 준비되지 않았다면 닫을 항목이 없음
  }

  struct file *file = current->FDT[fd];  // FDT에서 대상 파일 객체 확인

  if (file == NULL) {
    return;  // 이미 닫힌 fd는 추가 작업 불필요
  }

  struct file *stdin_file =
      syscall_get_std_file(STDIN_FILENO);  // STDIN 더미 파일 포인터 준비
  struct file *stdout_file =
      syscall_get_std_file(STDOUT_FILENO);  // STDOUT 더미 파일 포인터 준비

  if (file == stdin_file) {
    if (current->stdin_count > 0) {
      current->stdin_count--;  // STDIN 참조 카운트 감소
    }
    current->FDT[fd] = NULL;  // 현재 fd 슬롯을 비워서 닫힘을 표시
    return;                   // 표준 입력은 더 이상 처리할 필요 없음
  }

  if (file == stdout_file) {
    if (current->stdout_count > 0) {
      current->stdout_count--;  // STDOUT 참조 카운트 감소
    }
    current->FDT[fd] = NULL;  // 현재 fd 슬롯을 비워서 닫힘을 표시
    return;                   // 표준 출력은 더 이상 처리할 필요 없음
  }

  current->FDT[fd] = NULL;      // 일반 파일의 경우 우선 FDT에서 제거
  lock_acquire(&filesys_lock);  // 파일 객체 공유 보호를 위해 락 획득

  if (file->dup_count > 0) {
    file->dup_count--;  // 아직 다른 fd가 동일 파일을 참조하므로 참조만 감소
    lock_release(&filesys_lock);  // dup_count 조정 후 락 해제
    return;                       // 실제 파일은 닫지 않음
  }

  file_close(file);             // 더 이상 참조가 없으니 파일 닫기 수행
  lock_release(&filesys_lock);  // 파일 연산 후 락 해제
}

int syscall_write(int fd, const void *buffer, unsigned size) {
  // 사용자 버퍼가 유효한 커널 접근 범위인지 확인
  if (!only_user_addr(buffer)) syscall_exit(-1);
  // check_user_buffer(buffer, size, false);

  struct thread *current = thread_current();  // 현재 스레드 포인터 확보
  struct file *file = process_get_file(fd);   // fd에 대응되는 파일 객체 조회

  // STDIN 더미 파일 포인터 준비
  struct file *stdin_file = syscall_get_std_file(STDIN_FILENO);
  // STDOUT 더미 파일 포인터 준비
  struct file *stdout_file = syscall_get_std_file(STDOUT_FILENO);

  if (file == stdin_file) return -1;  // STDIN으로는 출력할 수 없으므로 실패

  // 아직 STDERR 출력은 지원하지 않으므로 실패 처리
  if (fd == STDERR_FILENO) return -1;

  if (file == stdout_file) {
    // STDOUT이 모두 닫힌 상태라면 출력 불가
    if (current->stdout_count == 0) return -1;
    putbuf(buffer, size);  // 콘솔에 버퍼 내용을 그대로 출력
    return size;           // 출력한 바이트 수 반환
  }

  if (file == NULL) return -1;  // 열려 있지 않은 fd이므로 실패

  return file_write(file, buffer, size);  // 파일에 데이터 쓰기 수행
}

int syscall_read(int fd, void *buffer, unsigned size) {
  // 사용자 버퍼가 커널에서 접근 가능한지
  // check_user_buffer(buffer, size, true);
  if (!only_user_addr(buffer)) syscall_exit(-1);

  // 페이지를 찾는다 - buffere 주소에 맞는 // 매 페이지마다.
  // 페이지가 null이면 그냥 진행
  // 페이지가 있는데 writable이 false면은 syscall_exit(-1) or return false

  struct thread *current = thread_current();  // 현재 스레드 포인터 확보
  struct file *file = process_get_file(fd);   // fd에 연결된 파일 객체 조회
  // STDIN 더미 파일 포인터 준비
  struct file *stdin_file = syscall_get_std_file(STDIN_FILENO);
  // STDOUT 더미 파일 포인터 준비
  struct file *stdout_file = syscall_get_std_file(STDOUT_FILENO);

  if (file == stdin_file) {
    if (current->stdin_count == 0) {
      return -1;  // STDIN이 모두 닫혔다면 읽기 불가
    }

    char *dst = (char *)buffer;  // 입력 문자를 저장할 버퍼 포인터 준비

    for (unsigned i = 0; i < size; i++) {
      dst[i] = input_getc();  // 키보드에서 한 글자씩 읽어 버퍼에 기록
    }

    return size;  // 요청한 길이만큼 읽었으므로 그대로 반환
  }
  // STDOUT/STDERR에서는 읽기를 지원하지 않음
  if (file == stdout_file || fd == STDERR_FILENO) return -1;

  // 열려 있지 않은 fd라면 실패
  if (file == NULL) return -1;

  // // 커널영역 복사
  // void *kbuf = palloc_get_page(PAL_ZERO);

  // 파일 시스템 접근 보호를 위해 락 획득
  // lock_acquire(&filesys_lock);
  // 파일에서 데이터 읽어오기
  int bytes_read = file_read(file, buffer, size);
  // 파일 연산 후 락 해제
  // lock_release(&filesys_lock);

  // if (bytes_read > 0) memcpy(buffer, kbuf, bytes_read);
  // 해제
  // palloc_free_page(kbuf);
  return bytes_read;  // 실제로 읽은 바이트 수 반환
}

int syscall_filesize(int fd) {
  // 파일 디스크립터 번호를 이용해 파일 객체 가져오기
  struct file *file = process_get_file(fd);

  // 유효하지 않은 fd이거나 파일이 열려 있지 않은 경우 -1 반환
  if (file == NULL) {
    return -1;
  }

  // 해당 파일의 크기(바이트 단위)를 반환
  return file_length(file);
}

/* 파일을 오픈하는 시스템콜 핸들러 */
int syscall_open(const char *file_name) {
  if (file_name == NULL) return -1;

  char *k_file = palloc_get_page(PAL_ZERO);
  if (!copy_in_string(k_file, file_name)) {
    palloc_free_page(k_file);
    syscall_exit(-1);
  }

  // 파일 시스템 접근을 위한 락 획득
  lock_acquire(&filesys_lock);

  // 파일 시스템에서 파일 열기 시도
  struct file *file = filesys_open(k_file);

  // 파일이 없거나 열기에 실패한 경우 -1 반환
  if (file == NULL) {
    lock_release(&filesys_lock);
    palloc_free_page(k_file);
    return -1;
  }

  // 현재 프로세스의 파일 디스크립터 테이블(FDT)에 파일 등록
  int fd = process_add_file(file);

  // 파일 등록에 실패한 경우 → 열린 파일 닫기
  if (fd == -1) {
    file_close(file);
  }
  // 파일 시스템 락 해제
  lock_release(&filesys_lock);
  palloc_free_page(k_file);

  // 파일 디스크립터 번호 반환, 실패 시 -1 반환
  return fd;
}

// 매핑이 안되어있는게 정상.
/* 파일을 삭제하는 시스템콜 핸들러 */
bool syscall_remove(const char *file) {
  if (file == NULL) return false;

  char *k_file = palloc_get_page(PAL_ZERO);
  if (!copy_in_string(k_file, file)) {
    palloc_free_page(k_file);
    syscall_exit(-1);
  }

  // 파일 시스템에 생성 시도
  bool success = filesys_remove(k_file);

  palloc_free_page(k_file);
  // 파일 시스템에서 해당 경로의 파일 삭제 시도, 성공 여부 반환
  return success;
}

/* 파일을 생성하는 시스템콜 핸들러 */
bool syscall_create(const char *file, unsigned initial_size) {
  if (file == NULL) syscall_exit(-1);

  char *k_file = palloc_get_page(PAL_ZERO);
  if (!copy_in_string(k_file, file)) {
    palloc_free_page(k_file);
    syscall_exit(-1);
  }

  // 파일 시스템에 생성 시도
  bool success = filesys_create(k_file, initial_size);

  palloc_free_page(k_file);
  return success;
}

pid_t syscall_fork(const char *name, struct intr_frame *f) {
  void *kbuf = palloc_get_page(PAL_ZERO);
  if (kbuf == NULL) syscall_exit(-1);

  if (!copy_in_string(kbuf, name)) {
    palloc_free_page(kbuf);
    syscall_exit(-1);
  }
  tid_t child = process_fork(kbuf, f);

  palloc_free_page(kbuf);

  // thread_create 실패 시 TID_ERROR가 내려오므로 PID_ERROR로 변환해 반환
  return child == TID_ERROR ? PID_ERROR : child;
}

int syscall_wait(int pid) { return process_wait(pid); }

// exec는 실패하면 그냥 -1, 기존 프로세스는 동작하게 하기 위함
// 페이지는 메모리가 교체되기때문에 상관없다.
int syscall_exec(const char *cmd_line) {
  // 사용자로부터 받은 문자열(cmd_line)을 복사할 커널 영역의 페이지를 할당
  // PAL_ZERO는 할당된 메모리를 0으로 초기화하라는 의미
  char *cmd_line_copy = palloc_get_page(PAL_ZERO);

  // 만약 메모리 할당에 실패했다면, exit 처리
  if (cmd_line_copy == NULL) return -1;

  // 사용자 영역 문자열을 안전하게 복사하면서 페이지 경계를 검사한다.
  if (!copy_in_string(cmd_line_copy, cmd_line)) {
    palloc_free_page(cmd_line_copy);
    return -1;
  }

  // 실제로 새로운 프로그램을 현재 프로세스 위에 실행
  // 실패하면 -1을 반환하므로, exit 처리
  if (process_exec(cmd_line_copy) == -1) {
    // palloc_free_page(cmd_line_copy);
    return -1;
  }

  // 여기까지 실행이 오면 안된다 - 라는 매크로
  NOT_REACHED();
}

/* 프로세스 종료 시스템콜 핸들러 */
void syscall_exit(int status) {
  struct thread *current_thread = thread_current();
  current_thread->exit_status = status;
  printf("%s: exit(%d)\n", current_thread->name, status);
  thread_exit();
}

/* 전원 종료(Pintos/QEMU 종료) 시스템콜 핸들러 */
void syscall_halt(void) {
  // 전원 끄기: Pintos/QEMU 종료
  power_off();
}

static void *syscall_mmap(void *addr, size_t length, int writable, int fd,
                          off_t offset) {
  // addr가 0인 경우 (Pintos의 일부 코드는 가상 페이지 0이 매핑되지 않았다고
  // 가정함)
  if (addr == NULL) return NULL;
  // addr가 커널 영억을 침범하는 경우
  if (is_kernel_vaddr(addr)) return NULL;
  // addr가 페이지 정렬(page-aligned)되어 있지 않은 경우
  if ((uint64_t)addr % PGSIZE != 0) return NULL;
  // length가 0일 경우
  if (length == 0) return NULL;
  // length보다 offset이 클 경우
  if (length < offset) return NULL;
  struct thread *cur = thread_current();
  // fd로 파일을 찾을 수 없는 경우
  if (check_fd(cur, fd) == -1) return NULL;
  struct file *file = cur->FDT[fd];
  // 파일이 콘솔 입출력(STDIN_FILENO 또는 STDOUT_FILENO)을 나타내는 경우
  if (IS_STDIO(file)) return NULL;
  // fd로 열린 파일의 길이가 0일 경우
  if (file_length(file) == 0) return NULL;
  /* 매핑하려는 가상 주소 범위(addr부터 addr + length까지)가 기존에 매핑된
     페이지 영역 (예: 코드, 데이터, 스택, 다른 mmap 영역)과 겹치는 경우 */
  for (void *i = addr; i < addr + length; i += PGSIZE) {
    if (spt_find_page(&cur->spt, i) != NULL) return NULL;
  }
  void *result = do_mmap(addr, length, writable, file, offset);

  return result;
}

// 명시적으로 mmap 해제
static void syscall_munmap(void *start_addr) {
  if (start_addr == NULL) return NULL;
  if (start_addr == 0) return NULL;
  if (is_kernel_vaddr(start_addr)) return NULL;

  do_munmap(start_addr);
}

static bool is_user_mapped(const void *u_addr) {
  if (u_addr == NULL || is_kernel_vaddr(u_addr)) return false;

  return pml4_get_page(thread_current()->pml4, u_addr) != NULL;
}

static bool only_user_addr(const void *u_addr) {
  if (u_addr == NULL || is_kernel_vaddr(u_addr)) return false;
  return true;
}

static bool check_user_buffer(void *buffer, size_t size, bool to_read) {
  uint8_t *ptr = (uint8_t *)buffer;
  for (size_t i = 0; i < size; i++) {
    if (!is_user_vaddr(ptr + i)) syscall_exit(-1);

    if (to_read) {
      // 커널이 이 주소로부터 읽기 → 반드시 매핑돼야 함
      if (!is_user_mapped(ptr + i)) syscall_exit(-1);
    } else {
      // 커널이 이 주소에 쓰기 → lazy page 허용
      // 단, 커널 주소만 거르기
      if (!only_user_addr(ptr + i)) syscall_exit(-1);
    }
  }
  return true;
}

static void copy_in(void *dst, const void *src, size_t size) {
  uint8_t *d = dst;
  const uint8_t *s = src;

  for (size_t i = 0; i < size; i++) {
    if (!is_user_mapped(s + i)) syscall_exit(-1);
    d[i] = s[i];
  }
}

static bool copy_in_string(void *k_addr, const char *user_str) {
  uint8_t *k_buf = (uint8_t *)k_addr;
  size_t i = 0;
  while (true) {
    if (!is_user_mapped(user_str + i)) return false;

    k_buf[i] = user_str[i];
    if (user_str[i] == '\0') return true;
    i++;

    if (i >= PGSIZE) return false;
  }
}
static int check_fd(struct thread *t, int fd) {
  // 잘못된 fd 접근
  if (fd < 0 || fd >= MAX_FD) return -1;
  // 없는 fd 접근
  if (t->FDT[fd] == NULL) return -1;
  return 0;
}