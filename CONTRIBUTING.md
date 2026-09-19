# ft_irc 협업 가이드라인

본 문서는 `ft_irc` 팀 프로젝트의 브랜치 관리, 커밋 컨벤션, 이슈 및 PR 프로세스를 규정합니다.

---

## 1. 프로젝트 칸반 보드 & 이슈 관리
- 모든 작업(기능 구현, 버그 수정, 문서 작성)은 GitHub **Issues**에 등록 후 시작합니다.
- 이슈 등록 시 우측 사이드바에서 **Projects(`ft_irc Sprint`)**와 **Assignees(담당자)**를 반드시 지정합니다.
- 작업 착수 시 칸반 보드의 카드를 **In Progress**로 이동합니다.

---

## 2. 브랜치 전략
- `main`: 최종 평가 제출용 안정 브랜치 (직접 push 절대 금지)
- `dev`: 개발 통합 브랜치 (직접 push 금지, PR 승인 후 병합)
- 작업 브랜치 명명 규칙: `<타입>/<작업자이름>-<기능명>-#<이슈번호>`
  - 기능 구현: `feat/sisung-poll-loop-#3`, `feat/jejeon-parser-#4`
  - 버그 수정: `fix/sisung-fd-leak-#12`
  - 문서 작업: `docs/jejeon-specs-#2`

---

## 3. 커밋 메시지 규칙 (Conventional Commits)
커밋 메시지는 `타입: 내용` 형식으로 작성합니다.

- `feat`: 새로운 기능 추가
- `fix`: 버그 수정
- `refactor`: 코드 리팩토링 (기능 변경 없음)
- `docs`: 문서 작성 및 수정 (README, docs 등)
- `chore`: Makefile, 빌드 스크립트, .gitignore 수정
- `test`: 테스트 코드 또는 nc 스크립트 추가

> 예시: `feat: implement partial packet buffering in Client`

---

## 4. PR 원칙 및 적정 크기
- **단일 책임 원칙 (Single Responsibility):** 1개의 PR에는 1개의 독립된 기능(또는 버그 수정)만 담습니다.
- **적정 코드량:** 100~300줄 내외를 권장하며 최대 400줄을 넘지 않도록 합니다.
  - 대규모 기능은 '인터페이스/헤더 정의 PR'과 '세부 로직 PR'로 쪼개어 제출합니다.

---

## 5. 작업 브랜치 필수 테스트 (PR 제출 전)
작업 브랜치는 완전히 독립된 환경입니다. PR을 올리기 전 해당 브랜치에서 다음 3단계를 반드시 자체 통과해야 합니다.

1. **컴파일 검증:**
   - 서브젝트 필수 플래그로 경고 없이 컴파일되는지 확인합니다.
   ```bash
   make re
   # 컴파일 옵션: -Wall -Wextra -Werror -std=c++98
   ```

2. **기능 및 엣지 케이스 검증:**
   - 터미널 1: `./ircserv 6667 <password>`
   - 터미널 2 (`nc` 또는 레퍼런스 클라이언트):
     - `nc -C 127.0.0.1 6667`로 접속 후 커맨드 분할 전송(`Ctrl+D`) 테스트
     - 누락된 인자나 잘못된 인자 전송 시 적절한 Numeric Reply 반환 여부 확인

3. **자원 누수 검사:**
   - 비정상 종료(`Ctrl+C`) 및 클라이언트 단절 시 fd 및 메모리 해제 확인
   ```bash
   valgrind --leak-check=full ./ircserv 6667 <password>
   ```

---

## 6. 작업 및 PR(Pull Request) 프로세스

1. **브랜치 생성:**
   ```bash
   git checkout dev
   git pull origin dev
   git checkout -b feat/<본인이름>-<기능명>-#<이슈번호>
   ```

2. **코드 작업, 테스트, 커밋:**
   - 5번 필수 테스트를 통과한 후 의미 있는 단위로 커밋합니다.

3. **원격 푸시:**
   ```bash
   git push origin feat/<본인이름>-<기능명>-#<이슈번호>
   ```

4. **PR 생성 (GitHub 웹):**
   - **Base:** `dev` ← **Compare:** `본인 작업 브랜치`
   - PR 본문에 **`Closes #이슈번호`** 반드시 명시 (머지 시 이슈 자동 종료 연동).
   - PR 템플릿 항목(작업 요약, RFC/서브젝트 근거, 로컬 테스트 결과) 작성.

5. **상호 코드 리뷰 체크리스트:**
   - [ ] C++98 외 최신 문법이 포함되지 않았는가?
   - [ ] 단 1개의 `poll()` 외부에서 직접 `send()`나 `recv()`를 부르고 있지 않은가?
   - [ ] 연결 해제 시 소켓 fd `close` 및 컨테이너 삭제가 누락되지 않았는가?

6. **승인 및 머지:**
   - 상대방의 **Approve(승인)** 1개가 있어야 머지 가능.
   - 리뷰 승인 확인 후 PR 작성자가 **Squash and merge** 실행.
   - 머지 완료 후 GitHub 웹에서 작업 브랜치 삭제.

7. **로컬 환경 정리:**
   ```bash
   git checkout dev
   git pull origin dev
   git branch -d feat/<본인이름>-<기능명>-#<이슈번호>
   ```
