# coap-thread-switch-light

CoAP over Thread 로 동작하는 **무선 스위치**와 **스마트 전등** 레퍼런스 설계 및 펌웨어.

기존 벽스위치 뒤에 들어가는 "이너릴레이" 형태의 **배터리 구동 무선 스위치**와,
버튼·로터리 엔코더를 갖춘 **패널 스위치**가 Thread 메시 위에서 **CoAP 바인딩**으로
전등을 직접 제어합니다. 전등은 **KC 인증 SMPS**로 DC 를 공급받는 **커스텀 LED+MCU
PCB**(튜너블 화이트)로, **색온도(CCT)·디밍**을 지원합니다. 전등 상태는
**CoAP→MQTT 브리지**를 통해 Home Assistant 같은 스마트홈 플랫폼에 노출됩니다.

```
 ┌────────────────┐   CoAP PUT /light (Thread)   ┌─────────────────┐
 │  Switch Node   │ ───────────────────────────▶ │   Light Node    │
 │  nRF54L15      │                              │ nRF54L / MGM240 │
 │  battery, SED  │ ◀───── CoAP ACK ──────────── │  ESP32-H2/C6    │
 └────────────────┘                              └───────┬─────────┘
   기존 벽스위치에 연결                                     │ CoAP Observe
   (dry-contact 감지)                                      ▼
                                          ┌──────────────────────────┐
                                          │  Thread Border Router     │
                                          │  + CoAP→MQTT Bridge       │
                                          └────────────┬─────────────┘
                                                       │ MQTT (HA Discovery)
                                                       ▼
                                          ┌──────────────────────────┐
                                          │      Home Assistant       │
                                          └──────────────────────────┘
```

## 구성 요소

| 경로 | 내용 |
|------|------|
| `docs/architecture.md` | 시스템 아키텍처, Thread 네트워크 토폴로지, 전력 설계 |
| `docs/coap-resource-model.md` | CoAP 자원 모델, 스위치–전등 바인딩 프로토콜, 페이로드 포맷 |
| `docs/hardware/switch-node.md` | 온오프 이너 스위치(nRF54L15) 하드웨어 설계·폼팩터·BOM |
| `docs/hardware/panel-switch.md` | 패널 스위치(버튼+엔코더) 하드웨어 설계·BOM |
| `docs/hardware/light-node.md` | 전등 노드(SMPS+모듈러 LED 보드) 하드웨어 설계·BOM |
| `docs/hardware/parts-candidates.md` | 전등 부품 후보표(LED·CC 드라이버·벅·SMPS·라디오 모듈) |
| `docs/hardware/power-budget.md` | 스위치 배터리 수명 분석 |
| `docs/home-assistant.md` | CoAP→MQTT 브리지 및 Home Assistant 연동 설계 |
| `firmware/common/` | 공유 CoAP 자원 정의 + 바인딩 모듈 |
| `firmware/switch/` | 온오프 이너 스위치 Zephyr 앱 (nRF54L15) |
| `firmware/switch-panel/` | 패널 스위치 Zephyr 앱 (버튼+로터리 엔코더) |
| `firmware/light/` | 전등 노드 Zephyr 앱 (튜너블 화이트 CW/WW, 멀티 보드) |
| `bridge/` | CoAP→MQTT 브리지 (Python / aiocoap) |

## 대상 플랫폼

- **스위치 노드(온오프/패널)**: nRF54L15 — 초저전력, Thread Sleepy End Device(SED)로
  배터리 수명 극대화. 전원은 타입별로 다름 — 이너릴레이 타입은 **코인셀 1차 전지**,
  월스위치(패널) 타입은 **리튬이온 + nPM1300 PMIC**(USB-C 충전 + 다중 레일 + 퓨얼게이지).
- **전등 노드**: KC 인증 SMPS 로 DC 를 공급받는 커스텀 LED+MCU PCB(튜너블 화이트). Zephyr가
  Thread를 지원하는 다음 MCU를 목표로 하며 보드 오버레이로 이식(색온도·디밍은 CW/WW PWM).
  - nRF54L15 / nRF52840 (Nordic) — CW/WW PWM 레퍼런스
  - ESP32-H2, ESP32-C6 (Espressif) — LEDC PWM
  - MGM240 / EFR32MG24 (Silicon Labs) — 기본 on/off, PWM 은 SDK 확인 후

## 빌드 개요

펌웨어는 [Zephyr RTOS](https://docs.zephyrproject.org) / [nRF Connect SDK](https://developer.nordicsemi.com) 기반입니다.

```bash
# 예: 온오프 이너 스위치 (nRF54L15 DK)
west build -b nrf54l15dk/nrf54l15/cpuapp firmware/switch

# 예: 패널 스위치 (버튼 + 로터리 엔코더)
west build -b nrf54l15dk/nrf54l15/cpuapp firmware/switch-panel

# 예: 전등 노드 (보드별)
west build -b nrf54l15dk/nrf54l15/cpuapp    firmware/light
west build -b xg24_dk/efr32mg24b310f1536im48 firmware/light
west build -b esp32c6_devkitc               firmware/light
west build -b esp32h2_devkitm               firmware/light
```

빌드 상세와 커미셔닝 절차는 각 앱의 `README.md`와 `docs/`를 참고하세요.

> 참고: 이 저장소는 레퍼런스 설계입니다. 실제 양산 PCB/기구는 `docs/hardware/`의
> 설계를 기반으로 별도 제작이 필요합니다.
