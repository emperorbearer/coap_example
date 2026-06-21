# nRF54 CoAP/LWM2M SCD40 Air Quality Monitor

Zephyr/NCS 애플리케이션으로, Sensirion SCD40 센서에서 CO2·온도·습도를 읽어
**CoAP/UDP** 위의 **LWM2M** 프로토콜로 서버에 전송합니다.

## 하드웨어

| 항목 | 내용 |
|------|------|
| MCU  | nRF54L15DK (`nrf54l15dk/nrf54l15/cpuapp`) 또는 nRF54H20DK |
| 센서 | Sensirion SCD40 (CO2 + 온도 + 습도, I2C) |
| 프로토콜 | CoAP/UDP → LWM2M |

### SCD40 배선

```
SCD40 VDD  → 3.3 V
SCD40 GND  → GND
SCD40 SDA  → I2C SDA 핀 (4.7 kΩ 풀업)
SCD40 SCL  → I2C SCL 핀 (4.7 kΩ 풀업)
SCD40 SEL  → GND (I2C 모드 선택)
```

> **핀 번호 확인 필수**: `boards/` 폴더의 `.overlay` 파일에서
> 실제 보드 회로도에 맞게 SDA/SCL 핀 번호를 수정하세요.

## LWM2M 오브젝트 매핑

| IPSO 오브젝트 | 리소스 5700 | 단위 | 설명 |
|--------------|------------|------|------|
| 3303/0 (Temperature) | 온도 | °C (Cel) | SCD40 온도 |
| 3304/0 (Humidity)    | 습도 | %RH       | SCD40 상대습도 |
| 3300/0 (Generic)     | CO₂ | ppm        | SCD40 CO₂ 농도 |

## 빌드 & 플래시

```bash
# nRF54L15DK
west build -b nrf54l15dk/nrf54l15/cpuapp -- \
    -DCONFIG_APP_LWM2M_SERVER_URI=\"coap://leshan.eclipseprojects.io:5683\" \
    -DCONFIG_APP_LWM2M_ENDPOINT_NAME=\"my-scd40-device\"
west flash

# nRF54H20DK
west build -b nrf54h20dk/nrf54h20/cpuapp
west flash
```

## 설정 옵션 (Kconfig)

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `CONFIG_APP_LWM2M_SERVER_URI` | `coap://leshan.eclipseprojects.io:5683` | LWM2M 서버 주소 |
| `CONFIG_APP_LWM2M_ENDPOINT_NAME` | `nrf54-scd40-001` | 디바이스 엔드포인트 이름 |
| `CONFIG_APP_SENSOR_READ_INTERVAL_SEC` | `30` | 센서 읽기 간격 (초) |

## 네트워크 연결

nRF54 시리즈는 LTE 내장 모뎀이 없습니다. 연결 방법:

- **nRF54L15**: OpenThread (IEEE 802.15.4) + Thread 보더 라우터 권장
  → `boards/nrf54l15dk_nrf54l15_cpuapp.conf`에서 OpenThread 옵션 주석 해제
- **nRF54H20**: nRF9160 LTE 모뎀 컴패니언 칩 또는 nRF7002 Wi-Fi 칩
- 개발/테스트: USB CDC-ECM (PC ↔ 보드 네트워크 공유)

## 로그 확인

```
*** nRF54 CoAP/LWM2M SCD40 Air Quality Monitor ***
SCD40 initialised on i2c@...
SCD40 periodic measurement started (5 s interval)
Network: IPv4 address obtained
LWM2M objects created: 3303/0 (temp), 3304/0 (hum), 3300/0 (CO2)
LWM2M: registered with server
SCD40: CO2=621 ppm  Temp=23.45 C  Hum=48.32 %RH
LWM2M updated → CO2=621 ppm  Temp=23.5 C  Hum=48.3 %RH
```

## 파일 구조

```
coap_example/
├── CMakeLists.txt
├── Kconfig
├── prj.conf
├── boards/
│   ├── nrf54l15dk_nrf54l15_cpuapp.conf     # nRF54L15 빌드 설정
│   ├── nrf54l15dk_nrf54l15_cpuapp.overlay  # nRF54L15 I2C 핀 설정
│   ├── nrf54h20dk_nrf54h20_cpuapp.conf     # nRF54H20 빌드 설정
│   └── nrf54h20dk_nrf54h20_cpuapp.overlay  # nRF54H20 I2C 핀 설정
└── src/
    ├── main.c      # LWM2M 클라이언트 + 센서 루프
    ├── scd40.h     # SCD40 드라이버 인터페이스
    └── scd40.c     # SCD40 I2C 드라이버 구현
```
