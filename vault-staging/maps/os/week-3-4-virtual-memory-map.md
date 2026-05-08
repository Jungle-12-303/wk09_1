---
type: Map
status: Active
week:
  - vm
systems:
  - Linux
  - Windows
  - PintOS
  - QEMU
tags:
  - domain:os
  - domain:pintos
  - domain:qemu
  - week:vm
  - topic:page-table
  - topic:frame
  - topic:swap
  - topic:mmap
  - layer:memory
  - layer:kernel
related_to:
  - "[[학습-가이드]]"
  - "[[concept-to-code-map]]"
---

# 3-4주차 Virtual Memory 지도

## 이 지도의 목적

PintOS 3-4주차 VM 학습에서 주소 변환, 페이지 테이블, 프레임, 페이지 폴트, swap, mmap 문서를 찾기 위한 링크 허브다.

## 먼저 볼 것

- [[address-translation-memory]]
- [[supplemental-page-table-knowledge]]
- [[page-fault-trace]]
- [[바이트-버퍼와-캐스팅-실험|바이트 버퍼와 캐스팅 실험]]

## 핵심 개념

- [[address-translation-memory]]: VA가 page table을 거쳐 실제 바이트 위치로 내려가는 큰 그림
- [[supplemental-page-table-knowledge]]: page table만으로는 기억할 수 없는 "원래 합법한 페이지" 정보

## 흐름 추적

- [[page-fault-trace]]: #PF가 OS 정책으로 이어지는 관문
- [[frame-eviction-trace]]: 빈 프레임이 없을 때 victim을 내보내고 새 페이지를 올리는 흐름

## 실험

- [[바이트-버퍼와-캐스팅-실험|바이트 버퍼와 캐스팅 실험]]

## 앞으로 만들 문서

- `swap-lab`
- `mmap-file-backed-page-knowledge`
