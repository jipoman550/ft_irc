# ft_irc

C++98 표준을 준수하는 소켓 기반 IRC(Internet Relay Chat) 서버 프로젝트입니다.

## 팀원 및 역할
- **sisung**: 서버 인프라 / `poll()` 이벤트 루프 / 소켓 세팅
- **jejeon**: IRC 프로토콜 파서 / 커맨드 핸들러 / 채널 관리

---

## 빌드 및 실행 방법

### 요구 사항
- C++98 지원 컴파일러 (`c++` 또는 `g++`, `clang++`)
- `make`

### 빌드
```bash
make
```

### 실행
```bash
./ircserv <port> <password>
# 예시: ./ircserv 6667 mypassword
```

### 클라이언트 접속 테스트 (`nc`)
```bash
nc -C 127.0.0.1 6667
```

---

## 협업 가이드
자세한 브랜치 전략, 커밋 메시지 규칙, PR 프로세스 및 코드 스타일 가이드는 [CONTRIBUTING.md](CONTRIBUTING.md)를 참고하세요.
