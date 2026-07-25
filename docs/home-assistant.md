# Home Assistant 연동: CoAP→MQTT 브리지

전등 상태를 Home Assistant(HA)에 노출하기 위한 브리지 설계다. CoAP 는 HA 가 기본
지원하지 않으므로, 전등의 CoAP 자원을 **MQTT 로 변환**하고 HA 의 **MQTT Discovery**
로 자동 등록한다. 구현은 `bridge/` 참조.

## 1. 배치

```
 Thread 메시 ── Light(FTD) ══ OTBR(Border Router) ══ LAN ══ MQTT Broker ══ Home Assistant
                                     │
                                CoAP↔MQTT Bridge
                              (OTBR 호스트에서 실행 권장)
```

- **OTBR(OpenThread Border Router)** 가 Thread↔IPv6 라우팅을 제공(예: Raspberry Pi).
- **브리지 서비스**(Python/aiocoap)가 같은 호스트에서 실행되어 전등의 IPv6 로 CoAP 접근.
- **MQTT 브로커**(예: Mosquitto)와 HA 는 기존 스마트홈 인프라 재사용.

## 2. 동작

### 2.1 상태 업스트림 (전등 → HA)

1. 브리지가 각 전등의 `GET /light/state` 를 **CoAP Observe** 로 구독.
2. 전등 상태 변경 시(스위치/브리지/로컬 어떤 경로든) Observe 통지 수신.
3. 브리지가 CBOR→JSON 변환 후 MQTT **state 토픽**에 publish → HA 엔티티 갱신.

### 2.2 명령 다운스트림 (HA → 전등)

1. HA 에서 전등 on/off(또는 밝기) → MQTT **command 토픽** publish.
2. 브리지가 수신 → 전등에 **CoAP** `PUT /light`(CBOR) 전송.
3. 전등 상태 변경 → Observe 통지 → state 토픽 반영(폐루프). HA UI 는 실제 상태로 확정.

명령을 즉시 낙관적 반영하지 않고 Observe 확인 후 반영하므로, 로컬 스위치 조작과
원격 명령이 섞여도 HA 상태가 실제와 어긋나지 않는다.

## 3. MQTT 토픽 & Discovery

기기별 고유 ID(예: Thread EUI-64 또는 설정된 node id)를 `<id>` 로 사용.

| 종류 | 토픽 |
|------|------|
| Discovery config | `homeassistant/light/<id>/config` (retain) |
| State | `coapbridge/light/<id>/state` |
| Command | `coapbridge/light/<id>/set` |
| Availability | `coapbridge/light/<id>/availability` |

Discovery 설정 페이로드(튜너블 화이트: 밝기 + 색온도):

```json
{
  "name": "CoAP Light <id>",
  "unique_id": "coap_light_<id>",
  "schema": "json",
  "state_topic": "coapbridge/light/<id>/state",
  "command_topic": "coapbridge/light/<id>/set",
  "availability_topic": "coapbridge/light/<id>/availability",
  "supported_color_modes": ["color_temp"],
  "brightness_scale": 254,
  "min_mireds": 153,
  "max_mireds": 370,
  "device": {
    "identifiers": ["coap_light_<id>"],
    "manufacturer": "coap-thread-switch-light",
    "model": "Light Node",
    "sw_version": "0.1.0"
  }
}
```

`schema: json` 을 사용하므로 state/command 페이로드는 HA JSON light 스키마를 따른다:

- State/Command 예: `{"state": "ON", "brightness": 254, "color_temp": 261}`
- 브리지가 이 HA JSON ↔ 전등 CBOR 상태 객체(`{"on":true,"bri":254,"ct":261}`)를 변환.

색온도 모드는 밝기를 함의한다. 디밍만 지원하는 전등은 `brightness`만, on/off 전용은
둘 다 생략한다.

## 4. 가용성(Availability)

- 브리지는 각 전등의 Observe 세션이 살아있으면 `online`, 통지 유실/타임아웃 시
  `offline` 을 availability 토픽에 publish → HA 에 연결 상태 표시.
- 브리지 자신의 LWT(Last Will) 로 브리지 다운 시 전체 offline 처리.

## 5. 전등 디스커버리 (브리지 입장)

브리지가 관리할 전등을 찾는 방법:

1. **설정 파일**: 전등 IPv6/ID 목록을 명시(가장 단순, 초기 버전 기본).
2. **멀티캐스트 디스커버리**: `GET /.well-known/core?rt=light.switch` 를 realm-local
   멀티캐스트로 질의해 응답 전등을 자동 등록(후속).

## 6. 보안 노트

- 초기 버전은 Thread 링크 보안 + 신뢰 LAN 을 전제.
- 강화: 전등↔브리지 CoAP 구간 **OSCORE**(객체 보안) 또는 DTLS 적용을 후속 과제로 둠.
- MQTT 는 브로커 인증/TLS 사용 권장.

## 7. 대안 (참고)

- **Matter over Thread**: HA 기본 지원이나 전등 펌웨어가 무거워지고 CoAP 바인딩과
  이중 스택이 됨. 본 프로젝트는 CoAP 를 핵심으로 두고 브리지 방식을 채택.
- 향후 게이트웨이에서 CoAP↔Matter 브리징도 가능하나 범위 밖.
