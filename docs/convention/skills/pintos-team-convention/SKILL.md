---
name: pintos-team-convention
description: 이 저장소의 팀 컨벤션을 한 번에 적용하는 통합 스킬. Pintos/C 코드 작성, 코드 구조, 주석 번역 및 추가, Python 스크립트 작성, 파일 배치, AI Q&A 문서화, 커밋 메시지, PR 설명이 필요할 때 사용한다.
---

# Pintos Team Convention

이 스킬은 이 저장소의 `docs/convention/*.md`를 작업 종류에 따라 선택 적용하는 진입점이다.
항상 모든 문서를 읽지 말고, 현재 작업과 직접 관련된 문서만 연다.

## 기본 흐름

1. 파일을 새로 만들거나 옮기면 `docs/convention/project-structure.md`를 먼저 확인한다.
2. Pintos/C 코드를 수정하면 아래 세 문서를 함께 적용한다.
   - `docs/convention/c-coding-standard.md`
   - `docs/convention/code-structure-convention.md`
   - `docs/convention/comment-convention.md`
3. Python 파일이나 테스트를 수정하면 `docs/convention/python-style.md`를 적용한다.
4. AI를 학습 도구로 사용하거나 Q&A를 남겨야 하면 `docs/convention/ai-qa-convention.md`를 적용한다.
5. 커밋 메시지가 필요하면 `docs/convention/commit-convention.md`를 적용한다.
6. PR 제목/설명이 필요하면 `docs/convention/pr-convention.md`를 적용한다.

## 작업별 선택 규칙

### Pintos/C 코드

다음 상황에서는 C 관련 문서를 함께 읽는다.

- `.c`, `.h` 파일을 수정할 때
- 함수/전역 변수/static helper 위치를 정할 때
- Pintos 원문 주석을 번역하거나 새 주석을 추가할 때

핵심 기준:

- 가독성을 우선하고, 문제가 있으면 빨리 실패하게 작성한다.
- 변수/함수 이름은 스네이크 케이스를 사용한다.
- 파일 내부 전역은 `static`으로 제한한다.
- 공개 흐름은 위쪽, 내부 구현 상세는 아래쪽에 둔다.
- 주석은 코드 옆이 아니라 코드 위에 둔다.
- `@lock` 주석은 번역 완료된 원문으로 간주하고 임의 수정/삭제하지 않는다.
- 새 주석은 한글로 작성한다.

### Python 코드

다음 상황에서는 Python 문서를 읽는다.

- `.py` 파일을 생성/수정할 때
- 테스트 스크립트, 자동화 스크립트, 보조 도구를 작성할 때

핵심 기준:

- 4칸 스페이스, 88자 제한을 기본으로 둔다.
- 함수/변수는 `snake_case`, 상수는 `UPPER_SNAKE_CASE`를 사용한다.
- 문자열은 기본적으로 쌍따옴표를 사용한다.

### 파일 배치

다음 상황에서는 프로젝트 구조 문서를 읽는다.

- 새 디렉터리나 파일 위치를 정할 때
- 문서, 테스트, 스크립트의 저장 위치가 애매할 때

이 저장소처럼 과제/문서/스크립트가 함께 있는 구조에서는 기존 디렉터리 관례를 우선 존중한다.

### AI Q&A 문서화

다음 상황에서는 AI Q&A 문서를 읽는다.

- AI에게 학습용 질문을 정리해야 할 때
- `docs/member/<id>/qNN-*.md` 형식의 기록을 남길 때

핵심 기준:

- 구현 코드 대리 작성 요청은 피한다.
- 질문과 답변은 개인 문서로 남긴다.
- 파일명은 `qNN-<slug>.md` 형식을 따른다.

### 커밋과 PR

다음 상황에서는 Git 관련 문서를 읽는다.

- 커밋 메시지를 작성하거나 제안할 때
- 브랜치 전략, PR 제목/설명, 리뷰 요청 문구가 필요할 때

핵심 기준:

- 커밋/PR 제목은 `<type>: <한국어 설명>` 형식을 사용한다.
- 스코프는 제목에 넣지 않는다.
- PR 설명은 변경 내용, 이유, 검증 방법, 리뷰 포인트 중심으로 쓴다.

## 빠른 적용 매핑

- C 구현: `c-coding-standard` + `code-structure-convention`
- C 주석 작업: `comment-convention`
- Python 스크립트: `python-style`
- 파일 위치 판단: `project-structure`
- AI 질문 기록: `ai-qa-convention`
- 커밋 메시지: `commit-convention`
- PR 설명: `pr-convention`

## 주의

- `docs/convention/codex-skill-guide.md`는 사용 방법 문서다. 실제 작업 규칙이 필요할 때는 위의 원본 컨벤션 문서를 읽는다.
- 현재 작업에 필요한 문서만 열어 컨텍스트를 줄인다.
