# MGM240P LwM2M Switch & Light 예제 (Zephyr + Leshan)

SparkFun Thing Plus Matter (MGM240P) 보드 2개를 사용하는 Zephyr RTOS 기반
LwM2M 예제입니다.

- **스위치 디바이스** (`apps/switch_device`) — 외부 스위치(버튼)의 상태를
  IPSO On/Off Switch 객체(**3342**)로 Leshan 서버에 보고합니다.
- **라이트 디바이스** (`apps/light_device`) — IPSO Light Control 객체(**3311**)를
  제공하며, Leshan 서버가 `3311/0/5850`(On/Off)에 Write 하면 GPIO에 연결된
  릴레이를 구동하여 전등을 켜고 끕니다.
- **브리지 스크립트** (`scripts/leshan_bridge.py`) — Leshan REST API로 스위치
  상태를 Observe 하고, 변경될 때마다 라이트 디바이스에 Write 하여
  "스위치 → 전등" 자동 제어를 완성합니다.

MGM240P는 Wi-Fi/이더넷이 없는 802.15.4(Thread) SoC이므로, 두 보드는
**OpenThread** 네트워크에 조인한 뒤 **Thread Border Router(OTBR)** 를 거쳐
IPv6/CoAP 로 Leshan 서버에 등록합니다.

## 아키텍처

```
[외부 스위치]                                   [릴레이 + 전등]
     │ GPIO (PB00)                                  │ GPIO (PB01)
┌────┴─────────┐                              ┌─────┴────────┐
│ MGM240P #1   │                              │ MGM240P #2   │
│ switch_device│                              │ light_device │
│ (LwM2M 3342) │                              │ (LwM2M 3311) │
└────┬─────────┘                              └─────┬────────┘
     │ Thread (802.15.4)            Thread (802.15.4)│
     └───────────────┐              ┌────────────────┘
              ┌──────┴──────────────┴──────┐
              │  Thread Border Router(OTBR)│
              └──────────────┬─────────────┘
                             │ IPv6 (CoAP/UDP 5683)
                   ┌─────────┴──────────┐
                   │   Leshan 서버      │◄── scripts/leshan_bridge.py
                   │ (등록/Observe/Write)│    (3342 Observe → 3311 Write)
                   └────────────────────┘
```

두 디바이스 모두 자신의 동작 상태를 LwM2M 리소스로 노출하므로, Leshan 웹
UI에서 항상 현재 상태를 Read/Observe 할 수 있고, 라이트는 서버에서 직접
Write 로 제어할 수도 있습니다.

## 하드웨어 배선

| 디바이스 | 신호 | MGM240P 핀 | Thing Plus 헤더 라벨 |
|---|---|---|---|
| 스위치 | 푸시버튼/스위치 입력 (내부 풀업, GND와 연결) | PB00 | A4 |
| 라이트 | 릴레이 모듈 IN (active-high) | PB01 | A3 |
| 공통 | 상태 표시 LED | PA08 | 온보드 파란 LED |

- 스위치: 버튼 한쪽을 **PB00**, 반대쪽을 **GND**에 연결합니다.
- 릴레이: 3.3 V 로직 호환 릴레이 모듈의 IN을 **PB01**에, GND/VCC를 보드에
  연결하고, 릴레이 접점에 전등을 연결합니다. (상용 전원(AC)을 다룰 경우
  감전·화재에 충분히 주의하세요.)
- 핀을 바꾸려면 각 앱의 `boards/sparkfun_thing_plus_matter_mgm240p.overlay`를
  수정하면 됩니다.

## 사전 준비

1. **Zephyr 개발 환경** (Zephyr 4.x + west + Zephyr SDK)
   — <https://docs.zephyrproject.org/latest/develop/getting_started/>
2. **Thread Border Router** — 예: Raspberry Pi + OTBR, 또는 OTBR Docker.
   두 앱의 `prj.conf`에 있는 OpenThread 설정(네트워크 이름, 채널, PAN ID,
   네트워크 키)을 **실제 OTBR의 Active Dataset과 동일하게** 맞춰야 합니다.
3. **Leshan 서버** — Thread 네트워크에서 도달 가능한 호스트(보통 OTBR과 같은
   호스트)에서 실행:

   ```bash
   wget https://ci.eclipse.org/leshan/job/leshan-ci/job/master/lastSuccessfulBuild/artifact/leshan-server-demo.jar
   java -jar leshan-server-demo.jar
   ```

   웹 UI: `http://<호스트>:8080`, CoAP 수신 포트: `5683`

## 빌드 및 플래시

각 앱의 `Kconfig` 기본값(`APP_LWM2M_SERVER_URL`)을 Leshan 호스트의 IPv6
주소로 바꾸거나, 빌드 시 `-D`로 지정합니다. Thread 디바이스에서 도달
가능한 주소(OTBR이 라우팅하는 글로벌/OMR 주소)를 사용해야 합니다.

```bash
# 스위치 디바이스
west build -p -b sparkfun_thing_plus_matter_mgm240p apps/switch_device \
    -d build_switch -- \
    -DCONFIG_APP_LWM2M_SERVER_URL=\"coap://[2001:db8::1]:5683\"
west flash -d build_switch

# 라이트 디바이스
west build -p -b sparkfun_thing_plus_matter_mgm240p apps/light_device \
    -d build_light -- \
    -DCONFIG_APP_LWM2M_SERVER_URL=\"coap://[2001:db8::1]:5683\"
west flash -d build_light
```

플래시는 J-Link(또는 Simplicity Commander)를 사용합니다. 부팅 후 UART
콘솔(115200 8N1)에서 로그와 함께 `ot` / `net` 셸 명령을 사용할 수
있습니다(예: `ot ipaddr`, `ot state`).

## 실행 흐름

1. 두 보드가 Thread 네트워크에 attach 한 뒤 Leshan에 등록됩니다.
   Leshan 웹 UI의 **Clients** 목록에 `mgm240-switch`, `mgm240-light`가
   나타납니다.
2. 브리지 스크립트를 실행합니다 (Python 3.10+, 외부 패키지 불필요):

   ```bash
   python3 scripts/leshan_bridge.py --leshan http://localhost:8080 \
       --switch mgm240-switch --light mgm240-light
   ```

3. 스위치 디바이스의 버튼을 누르면:
   - `3342/0/5500` 상태가 토글되어 Leshan에 통지(Notify)되고,
   - 브리지가 이를 받아 `mgm240-light`의 `3311/0/5850`에 Write,
   - 라이트 디바이스가 릴레이 GPIO를 구동하여 전등이 켜지거나 꺼집니다.
4. Leshan 웹 UI에서 직접 `3311/0/5850`을 Write 해서 전등을 수동 제어할
   수도 있고, `3311/0/5852`(On time)로 점등 누적 시간을 읽을 수도 있습니다.

## 커스터마이즈

| 항목 | 위치 |
|---|---|
| Leshan 서버 주소 | 각 앱 `Kconfig`의 `APP_LWM2M_SERVER_URL` (또는 빌드 시 `-DCONFIG_...`) |
| 엔드포인트 이름 | `APP_LWM2M_ENDPOINT` |
| 버튼 토글/레벨 모드 | 스위치 앱 `APP_SWITCH_TOGGLE_MODE` (`y`: 푸시버튼 토글, `n`: 슬라이드 스위치 레벨 추종) |
| GPIO 핀 | 각 앱 `boards/*.overlay` |
| Thread 네트워크 파라미터 | 각 앱 `prj.conf`의 `CONFIG_OPENTHREAD_*` |

## 트러블슈팅

- **Leshan에 등록이 안 됨** — 콘솔에서 `ot state`로 attach 여부(child/router)
  확인 → `ot ipaddr`로 주소 확인 → Leshan 호스트에서 해당 주소로
  `ping -6` 테스트. UDP 5683이 방화벽에 막혀 있지 않은지 확인하세요.
- **Thread attach 실패** — `prj.conf`의 네트워크 키/채널/PAN ID가 OTBR의
  Active Dataset과 정확히 일치하는지 확인하세요 (`ot dataset active`).
- **브리지가 반응 없음** — Leshan UI에서 두 클라이언트가 모두 온라인인지
  확인하고, 스크립트 출력에서 observe 시작 메시지를 확인하세요.

## 참고

- 본 예제는 평문 CoAP(NoSec)를 사용합니다. 실제 배포 시에는
  `CONFIG_LWM2M_DTLS_SUPPORT`와 PSK를 설정하고 Leshan에 보안 정보를
  등록하여 DTLS(coaps, 5684)로 전환하는 것을 권장합니다.
- IPSO 객체: [3342 On/Off Switch](https://github.com/OpenMobileAlliance/lwm2m-registry/blob/prod/3342.xml),
  [3311 Light Control](https://github.com/OpenMobileAlliance/lwm2m-registry/blob/prod/3311.xml)
