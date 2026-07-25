# CoAP 자원 모델 및 바인딩 프로토콜

이 문서는 스위치·전등·브리지가 공유하는 애플리케이션 프로토콜을 정의한다.
펌웨어 상수는 `firmware/common/coap_resources.h` 에 반영된다.

## 1. 페이로드 포맷

기본 포맷은 **CBOR**(`application/cbor`, content-format 60)를 사용한다. 임베디드에서
가볍고, 브리지에서 JSON 으로 무손실 변환된다. 디버깅 편의를 위해 전등은 `Accept` 헤더에
따라 **JSON**(content-format 50)도 응답할 수 있다.

상태 객체 스키마(논리 구조, JSON 표기). 이 전등은 **튜너블 화이트**(밝기 + 색온도):

```json
{
  "on":    true,          // 필수. 전원 on/off
  "bri":   254,           // 선택. 마스터 밝기 1..254 (0 은 사용 안 함), 미지원 시 생략
  "ct":    261,           // 선택. 색온도 mireds (153≈6500K 쿨 ~ 370≈2700K 웜)
  "src":   "switch",      // 선택. 마지막 변경 출처: "switch" | "bridge" | "local" | "boot"
  "seq":   1234           // 선택. 단조 증가 시퀀스(상태 최신성 판별용)
}
```

미지원 필드는 생략한다. 전등은 자신이 지원하는 기능만 노출한다:

| 기능 | 필드 | 비고 |
|------|------|------|
| 디밍 | `bri` | 마스터 밝기 |
| 색온도 | `ct` | 튜너블 화이트, mireds(153–370) |

**부분 갱신 규칙**: `PUT` 는 포함된 필드만 적용한다. 색온도 변경은 자동으로 `on:true` 를
함의한다(꺼진 상태에서 색온도만 바꾸면 켜짐). `bri` 는 색온도와 독립적인 마스터 밝기이며,
전등은 `ct` 로 CW/WW 비율을, `bri` 로 전체 밝기를 정한다.

## 2. 전등 노드(Light Node) 자원

| 자원 | 메서드 | 설명 |
|------|--------|------|
| `/light` | `GET` | 현재 상태 객체 반환 |
| `/light` | `PUT` | 상태 설정. 부분 갱신 허용(포함된 필드만 적용) |
| `/light` | `POST` | **토글**. 페이로드 없으면 on↔off 반전 |
| `/light/state` | `GET` + **Observe** | 상태 자원(관측 가능). 변경 시 구독자에게 통지 |
| `/.well-known/core` | `GET` | 자원 디스커버리(CoRE Link Format). 아래 참조 |

### 2.1 디스커버리 (`/.well-known/core`)

전등은 자신을 아래처럼 광고한다(RFC 6690 CoRE Link Format):

```
</light>;rt="light.switch";if="core.a";ct="60 50",
</light/state>;rt="light.switch";obs
```

- `rt="light.switch"` : 스위치가 바인딩 대상으로 필터링할 리소스 타입.
- `obs` : Observe 지원 표시.

스위치는 `GET coap://[realm-local-multicast]/.well-known/core?rt=light.switch` 로
전등을 검색할 수 있다.

### 2.2 응답 코드

| 상황 | 코드 |
|------|------|
| GET/PUT/POST 성공 | `2.05 Content` / `2.04 Changed` |
| 잘못된 페이로드 | `4.00 Bad Request` |
| 미지원 content-format | `4.15 Unsupported Content-Format` |
| 자원 없음 | `4.04 Not Found` |

## 3. 스위치 노드(Switch Node) 동작

스위치는 서버 자원을 최소화(배터리)하고 주로 **클라이언트**로 동작한다.

### 3.1 입력 → 전송

벽스위치 접점 상태가 바뀌면:

- **maintained(유지형) 스위치**: 새 물리 상태를 그대로 전달 → `PUT /light {"on": <state>, "src":"switch"}`.
- **momentary(순간형) 스위치**: 누름 이벤트마다 → `POST /light` (토글).

기본은 유지형으로 가정(한국 벽스위치 다수). 빌드/커미셔닝 설정으로 선택 가능.

전송은 **Confirmable(CON)**. ACK 수신 시 성공. 재전송은 CoAP 표준 지수 백오프
(ACK_TIMEOUT 기본 2s, MAX_RETRANSMIT 4)를 따르되, 배터리 절약을 위해 상한을 둔다.

### 3.2 (선택) 스위치 상태 자원

원할 경우 스위치도 최소 자원 `/sw`(GET)로 마지막 물리 상태/배터리를 노출할 수 있으나,
SSED 특성상 요청에 즉시 응답이 어려우므로 기본 비활성. 배터리 잔량은 스위치가 능동적으로
브리지/전등에 주기 보고(예: 하루 1회 `POST /telemetry`)하는 방식을 권장.

## 4. 바인딩 (스위치 → 전등)

바인딩은 "이 스위치가 어떤 전등(들)을 제어하는가"를 정의한다.

### 4.1 바인딩 레코드

NVS(설정 저장소)에 저장되는 레코드:

```c
struct binding {
    otIp6Address target;     // 전등 유니캐스트 또는 그룹 멀티캐스트 주소
    char         uri[24];    // 대상 자원, 기본 "light"
    uint8_t      mode;       // 0=PUT(state), 1=POST(toggle)
    uint8_t      flags;      // bit0: confirmable
};
```

최대 N개(기본 4)의 바인딩을 지원 → 한 스위치가 여러 전등(그룹)을 제어 가능.

### 4.2 바인딩 생성 방법

1. **디스커버리 기반(권장)**: 커미셔닝 버튼 누름 → 스위치가 멀티캐스트로
   `GET /.well-known/core?rt=light.switch` → 응답한 전등을 후보로 저장.
   단일 전등이면 자동 바인딩, 복수면 커미셔닝 앱에서 선택.
2. **그룹 멀티캐스트**: 여러 전등을 하나의 realm-local 멀티캐스트 그룹에 가입시키고,
   스위치는 그 그룹 주소로 Non-confirmable 전송(단체 제어).
3. **수동**: CLI/커미셔닝 앱이 전등 IPv6 를 스위치에 직접 기록.

### 4.3 그룹 제어 시 신뢰성

멀티캐스트는 ACK 가 없다(NON). 단체 제어의 확실성이 필요하면 각 전등에 유니캐스트 CON
을 순차 전송하는 모드도 제공(바인딩 레코드 다중 등록).

## 5. 브리지 인터페이스 (요약)

브리지는 전등의 `GET /light/state` 를 **Observe** 로 구독하고, HA 명령은
`PUT /light` 로 전달한다. 상세는 `docs/home-assistant.md`.

## 6. 상수 정의 (펌웨어 공유)

| 이름 | 값 | 설명 |
|------|-----|------|
| `COAP_URI_LIGHT` | `"light"` | 전등 제어 자원 |
| `COAP_URI_LIGHT_STATE` | `"light/state"` | 관측 가능 상태 자원 |
| `COAP_RT_LIGHT` | `"light.switch"` | 리소스 타입 |
| `COAP_CT_CBOR` | `60` | CBOR content-format |
| `COAP_CT_JSON` | `50` | JSON content-format |
| `LIGHT_KEY_ON` | `"on"` | 상태 키 |
| `LIGHT_KEY_BRI` | `"bri"` | 밝기 키 |
| `LIGHT_KEY_CT` | `"ct"` | 색온도 키(mireds) |
| `LIGHT_KEY_SRC` | `"src"` | 출처 키 |
| `LIGHT_KEY_SEQ` | `"seq"` | 시퀀스 키 |

이 값들은 `firmware/common/coap_resources.h` 에 C 매크로로 정의된다.
