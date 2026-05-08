---
type: Map
status: Active
week:
  - user-programs
systems:
  - Linux
  - Windows
  - PintOS
  - QEMU
tags:
  - domain:os
  - domain:pintos
  - domain:qemu
  - week:user-programs
  - topic:syscall
  - topic:process
  - topic:elf
  - topic:fd
  - layer:user
  - layer:kernel
related_to:
  - "[[학습-가이드]]"
---

# 2주차 User Programs 지도

## 이 지도의 목적

PintOS 2주차 User Programs 학습에서 유저 모드, ELF 로딩, 시스템 콜, 파일 디스크립터, 프로세스 생명주기 문서를 찾기 위한 링크 허브다.

## 먼저 볼 것

- [[syscall-end-to-end]]
- [[cpu-register-execution]]

## 핵심 개념

- [[cpu-register-execution]]

## 흐름 추적

- [[syscall-end-to-end]]
- [[user-pointer-validation-trace]]: syscall이 “유저 메모리”를 만질 때 반드시 필요한 안전장치

## 실험

- [[바이트-버퍼와-캐스팅-실험|바이트 버퍼와 캐스팅 실험]]
- [[argument-passing-lab]]: argv/argc가 스택 바이트로 어떻게 만들어지는지

## 앞으로 만들 문서

- `elf-loader-knowledge`
- `file-descriptor-knowledge`
- `process-wait-exit-trace`
