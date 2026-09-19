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

## 4. 작업 및 PR(Pull Request) 프로세스
1. **브랜치 생성**:
   ```bash
   git checkout dev
   git pull origin dev
   git checkout -b feat/<본인이름>-<기능명>-#<이슈번호>
   ```
2. **코드 작업 및 커밋**:
   - 단위 작업 완료 시 의미 있는 커밋 메시지로 커밋.
3. **원격 푸시**:
   ```bash
   git push origin feat/<본인이름>-<기능명>-#<이슈번호>
   ```
4. **PR 생성 (GitHub 웹)**:
   - Base: dev ← Compare: 본인 작업 브랜치
   - PR 본문에 `Closes #이슈번호` 반드시 명시 (머지 시 이슈 자동 종료 연동).
   - PR 템플릿 항목(작업 내용, RFC 근거, 테스트 방법) 충실히 작성.
5. **상호 코드 리뷰 및 머지**:
   - 상대방의 Approve(승인) 1개가 있어야 머지 가능.
   - 리뷰 승인 확인 후 PR 작성자가 Squash and merge 실행.
   - 머지 완료 후 로컬 및 원격의 작업 브랜치 삭제.
