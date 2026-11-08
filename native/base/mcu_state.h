#pragma once
#include <stdint.h>

namespace McuState
{
    // 통신 문자열/명령 ID와 무관한 공통 정지 사유. 최초 사유를 보존한다.
    enum class StopReason : uint16_t {
        None = 0, Emergency = 1, InvalidState = 2,
        UnsupportedDevice = 3, InvalidCommand = 4
    };
    struct Status
    {
        /// @brief MCU의 초기 설정 완료 여부. get_config 응답의 is_config에 대응한다.
        /// false는 아직 설정되지 않은 상태다. 이후 finalize_config를 정상 처리하면 true로 전환한다.
        /// 통신 연결 성공이나 프린터 전체의 Ready 상태를 뜻하지 않는다.
        bool configured = false;

        /// @brief 호스트가 전달한 초기 설정의 CRC32 값. get_config 응답의 crc에 대응한다.
        /// 이후 finalize_config에서 저장하고, 호스트가 재연결 시 자신의 설정과 같은지 비교한다.
        /// 펌웨어 파일의 CRC나 통신 프레임 CRC가 아니다. 초기값 0만으로 설정 여부를 판단하지 않는다.
        uint32_t config_crc = 0;

        /// @brief MCU가 오류 등으로 정지 상태에 들어갔는지 나타낸다. is_shutdown에 대응한다.
        /// 현재는 논리 상태이며, true를 대입하는 것만으로 실제 모터/히터 출력이 차단되지는 않는다.
        /// 출력 차단·후속 제어 명령 차단·호스트 알림은 별도 정지 처리에서 구현해야 한다.
        bool shutdown = false;
        StopReason stop_reason = StopReason::None;

        /// @brief 실제 구현하고 확보한 이동 큐의 총 슬롯 수. get_config의 move_count에 대응한다.
        /// 현재 대기 중인 이동 개수나 남은 빈 슬롯 수가 아니다. 호스트가 전송 예약 용량을 판단할 때 쓴다.
        /// 아직 이동 큐를 구현하지 않았으므로 0이다. uint16_t는 0~65535 범위의 부호 없는 정수다.
        uint16_t move_count = 0;
    };

    // Status 복사본을 반환. 반환받은 값의 필드를 변경해도 MCU 내부 상태는 변경되지 않는다.
    Status status();

    // 장치 번호 범위를 1회 예약. count = 0도 가능. 
    // 설정 완료/정지 상태 또는 중복 예약이면 거부하고, 정지 상태로 전환함.
    // count는 사용할 번호 개수다. 예: count=3이면 OID 0,1,2가 유효하다.
    // 성공 시 true, 거부 시 false를 반환한다. 실제 장치 객체를 생성하는 동작은 아니다.
    bool allocate_oids(uint8_t count);

    // 실제 장치 등록 명령에서 번호 범위를 검사할 때 사용.
    // oid는 검사할 장치 번호다. 범위에 속해도 해당 장치가 실제 등록됐다는 뜻은 아니다.
    bool oid_in_range(uint8_t oid);
    // 실제 장치 설정을 지원하기 전에는 OID 0개인 기반 시험 설정만 확정한다.
    bool finalize_config(uint32_t crc);
    void shutdown(StopReason reason);
    // 정지 후에만 설정을 비운다. 시계와 transport sequence는 초기화하지 않는다.
    bool config_reset();
    // max_duration은 자동 안전 복귀 시간. 
    bool configure_digital_output(uint8_t oid, uint32_t pin, bool initial_value, bool shutdown_value, uint32_t max_duration);
    bool update_digital_output(uint8_t oid, bool value);
    bool queue_digital_output(uint8_t oid, uint32_t clock, bool value);
    // 예약 시각에 도달한 출력을 처리. 실행 실패로 새로 정지한 경우, false 반환
    bool poll_outputs(uint32_t now);
}
