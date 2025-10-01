# **📖 Team Convention Guide**

이 문서는 우리 팀이 협업을 할 때 지켜야 할 **Commit / PR / Branch / Kanban / Workflow 규칙**을 정리한 가이드입니다.

모든 팀원은 아래 규칙을 따라 일관된 협업 문화를 만들어 갑니다.

---

## **🔖 Commit Convention**

|**Type**|**설명**|
|---|---|
|**Feat**|새로운 기능 추가|
|**Fix**|버그 수정, 오타 수정|
|**Refactor**|코드 리팩토링 (기능 변화 없음)|
|**Design**|UI/UX 변경 (CSS, 디자인 수정 등)|
|**Comment**|주석 추가 및 변경|
|**Style**|코드 포맷팅 (세미콜론 누락, 공백 등)|
|**Test**|테스트 코드 추가, 수정, 삭제 (비즈니스 로직 변경 없음)|
|**Chore**|빌드 스크립트, 패키지 매니저, asset 등 기타 작업|
|**Init**|프로젝트 초기 설정|
|**Rename**|파일/폴더명 수정 및 이동|
|**Remove**|파일 삭제|

> 예시:

```
feat: 사용자 로그인 기능 추가
fix: 잘못된 S3 업로드 경로 수정
refactor: UserService 의존성 구조 리팩토링
```



---
## **📦 Branch Convention**
브랜치 이름은 다음과 같은 형식을 따릅니다.

```
이름/작업분류/작업내용
```

예시:
```
seungjun/feat/auth-login
seungjun/fix/spt-table
```

---

## **📑 PR Convention**

PR을 올릴 때는 다음 형식을 지킵니다.
1. **목적** → 무엇을 해결하기 위한 PR인지
2. **접근 전략** → 문제 해결을 위한 접근 방법
3. **구현부** → 핵심 구현 내용
4. **트러블** → 작업 중 발생한 문제와 해결 방법
5. **고려할 점 (option)** → 추가로 고민해야 할 부분
6. **후기 (option)** → 작업 소감

예시
```
## 목적
사용자 로그인 기능 구현

## 접근 전략
JWT 기반 인증 방식을 적용

## 구현부
- User 엔티티 및 AuthService 추가
- 로그인 API (`/auth/login`) 구현

## 트러블
- 토큰 만료 처리 문제 → refresh token 전략 도입
```



---

## **📊 Kanban Convention**

우리 팀의 칸반 보드 진행 흐름은 아래와 같습니다.
1. **할 일 발견** → 이슈 발생 시 기록
2. **회의** → 우선순위 및 담당자 지정
3. **Todo** → 할 일 목록 등록
4. **In Progress** → 현재 진행 중
5. **Review** → 코드 리뷰 단계 (PR 상태)
6. **Done** → 작업 완료 및 병합

---

## **⚙️ Work Flow**

작업을 진행할 때는 다음 절차를 따릅니다.
1. **작업 시작 전**

```
git pull origin dev
git checkout -b 이름/작업분류/작업내용
```

2. **작업 중**

    - 의미 있는 단위마다 commit
    - 필요 시 git pull origin dev으로 최신화
    - 충돌 발생 시 해결 후 push

3. **작업 완료 후**
    - 브랜치 push
    - Pull Request 생성
    - 코드 리뷰 → 승인 후 main 병합
