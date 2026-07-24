# 시스템 아키텍처

## 1. 개요

여러 Thread 노드와 한 개의 브리지로 구성됩니다.

| 역할 | 노드 | Thread 역할 | 전원 |
|------|------|-------------|------|
| 입력(온오프) | Inner Switch | **SSED** | 배터리 |
| 입력(패널) | Panel Switch (버튼+엔코더) | **SSED** | 배터리 |
| 출력(전등) | Light Node (SMPS+커스텀 LED PCB) | **FTD/Router** (상시 라우터) | 상시 전원(DC) |
| 게이트웨이 | Border Router + Bridge | Leader/Border Router | 상시 전원 |

두 종류의 스위치(온오프 이너 스위치, 버튼+엔코더 패널 스위치)는 동일한 SSED 전력
전략과 **공유 바인딩 모듈**(`firmware/common/binding.c`)을 사용한다. 전등은 릴레이가
아니라 RGBW LED 를 PWM 으로 직접 구동해 **색상·디밍**을 제공한다.

핵심 아이디어:

1. **기기–기기 제어는 클라우드/허브를 거치지 않는다.** 스위치는 전등의 IPv6 주소로
   직접 CoAP 요청을 보낸다(로컬 바인딩). 인터넷이 끊겨도 벽스위치는 동작한다.
2. **상태 가시성은 브리지가 담당한다.** 전등의 상태 자원을 CoAP Observe 로 구독해
   MQTT 로 변환하고, Home Assistant 에 표준 방식으로 노출한다.

## 2. Thread 네트워크

- 단일 Thread 네트워크(하나의 Network Key / PAN ID / Channel).
- **Light Node = Full Thread Device(FTD)**, 라우터 자격을 가지고 항상 깨어 있으며,
  스위치(SSED)의 부모(parent) 역할을 겸할 수 있어 스위치의 응답성이 좋아진다.
- **Switch Node = SSED**: 평소 라디오를 끄고 깊은 슬립. 부모와 주기적으로 동기화하며,
  로컬 입력(벽스위치 토글) 발생 시 즉시 깨어나 CoAP 요청을 전송한다.
- **Border Router(OTBR)** 가 Thread ↔ IPv6(Wi-Fi/Ethernet) 라우팅을 담당한다.
  브리지 서비스는 OTBR 와 같은 호스트(예: Raspberry Pi)에서 실행하는 것을 권장.

```
        (배터리, 대부분 슬립)              (상시 전원, 라우터)
   Switch(SSED) ── parent link ── Light(FTD/Router) ── mesh ── OTBR(Leader)
        │                                                        │
        └──────── CoAP 애플리케이션 트래픽 (양방향) ──────────────┘
```

## 3. 제어 흐름

### 3.1 로컬 제어 (스위치 → 전등)

1. 사용자가 기존 벽스위치를 토글 → 스위치 노드 GPIO 상태 변화(엣지 인터럽트)로 wake.
2. 스위치는 디바운스 후 바인딩된 전등 주소로 **Confirmable** `PUT /light` 전송
   (`{"on": true}` 또는 토글 명령).
3. 전등이 릴레이/TRIAC 를 제어하고 CoAP ACK 응답. 스위치는 ACK 확인 후 다시 슬립.
4. 전등의 상태 자원(`/light/state`)이 갱신되면서 Observe 구독자(브리지)에게 통지.

### 3.2 클라우드/앱 제어 (HA → 전등)

1. HA 에서 전등 on/off → MQTT command 토픽 publish.
2. 브리지가 이를 받아 전등에 **CoAP** `PUT /light` 전송.
3. 전등 상태 변경 → Observe 통지 → 브리지가 MQTT state 토픽으로 반영 → HA UI 갱신.

### 3.3 상태 동기화

전등이 **단일 진실 소스(single source of truth)**. 스위치·HA·브리지는 모두 전등의
상태를 반영할 뿐이며, 상태 변경은 언제나 전등에서 Observe 통지로 전파된다. 이렇게 하면
로컬 제어와 원격 제어가 섞여도 상태가 어긋나지 않는다.

## 4. 전력 설계 (스위치 노드)

배터리 수명이 이 프로젝트의 핵심 제약이다.

- **라디오 정책**: SSED. 슬립 중 라디오 오프, 부모와의 동기화 주기(CSL period)를
  수백 ms~수 초로 설정해 평균 전류를 최소화.
- **입력 감지**: 벽스위치 접점을 GPIO SENSE(래치) 로 감시. 상태 변화 시에만 CPU wake.
  풀업/풀다운은 누설 최소화를 위해 이벤트 시에만 능동화하거나 큰 저항을 사용.
- **송신 예산**: 토글 1회당 CoAP 왕복(≈수십 ms 라디오 on). 하루 수십 회 조작 가정 시
  평균 소비는 슬립 전류가 지배. CR2032(약 220mAh) 기준 수년 목표.
- **자세한 계산**: `docs/hardware/switch-node.md` 참고.

## 5. 커미셔닝 / 바인딩

1. **네트워크 조인**: 공장 초기 또는 버튼 트리거로 Thread 커미셔닝(예: OTBR 의
   ephemeral key, 또는 사전 공유 데이터셋). nRF54L15 는 nRF Connect SDK 의
   Thread 커미셔닝 사용.
2. **바인딩**: 스위치가 제어할 전등을 지정. 방법은 두 가지를 제공:
   - **자원 디렉터리/멀티캐스트 디스커버리**: 스위치가 `GET /.well-known/core?rt=light`
     를 realm-local 멀티캐스트로 질의해 전등을 찾음.
   - **수동 바인딩**: 커미셔닝 앱/CLI 로 전등 IPv6 를 스위치 NVS 에 기록.
   - 바인딩 대상은 유니캐스트 주소(또는 그룹 멀티캐스트)로 저장되어 재부팅에도 유지.

바인딩 데이터 포맷과 저장 방식은 `docs/coap-resource-model.md` 참고.

## 6. 신뢰성 / 엣지 케이스

- **전등 미응답**: 스위치는 CoAP CON 재전송(기본 지수 백오프) 후 실패 시 LED/피드백.
  상태 불일치는 다음 Observe 통지로 자동 정정.
- **전원 복구**: 전등은 부팅 시 마지막 상태를 NVS 에서 복원(설정 가능: on/off/last).
- **네트워크 분리**: 로컬 제어는 border router 없이도 동작. HA 가시성만 일시 중단.
- **보안**: Thread 링크 레이어 암호화 + CoAP 는 필요 시 DTLS(OSCORE 대안 검토).
  초기 버전은 Thread 네트워크 보안에 의존하고, 브리지↔전등 구간 OSCORE 는 후속 과제.
