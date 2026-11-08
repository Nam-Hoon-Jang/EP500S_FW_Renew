#include "klippy_codec.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

using KlippyCodec::Status;
int main() {
    uint8_t data[5];
    for (uint32_t value : {0u,95u,96u,127u,128u,300u,12287u,12288u,
                          1572863u,1572864u,201326591u,201326592u,UINT32_MAX}) {
        const auto encoded=KlippyCodec::encode_u32(value,data,5);
        uint32_t decoded=0;
        const auto result=KlippyCodec::decode_u32(data,encoded.bytes_used,decoded);
        assert(encoded.status==Status::Ok && result.status==Status::Ok);
        assert(result.bytes_used==encoded.bytes_used && decoded==value);
    }
    for (int32_t value : {INT32_MIN,-67108865,-67108864,-524289,-524288,
                         -4097,-4096,-33,-32,-1,0,95,96,INT32_MAX}) {
        const auto encoded=KlippyCodec::encode_i32(value,data,5);
        int32_t decoded=0;
        const auto result=KlippyCodec::decode_i32(data,encoded.bytes_used,decoded);
        assert(encoded.status==Status::Ok && result.status==Status::Ok);
        assert(decoded==value);
    }
    auto encoded=KlippyCodec::encode_u32(96,data,5);
    assert(encoded.bytes_used==2 && data[0]==0x80 && data[1]==0x60);
    encoded=KlippyCodec::encode_u32(300,data,5);
    assert(encoded.bytes_used==2 && data[0]==0x82 && data[1]==0x2C);
    uint32_t output=123;
    const uint8_t negative[]{0x7F};
    assert(KlippyCodec::decode_u32(negative,1,output).status==Status::Ok && output==UINT32_MAX);
    const uint8_t truncated[]{0x82};
    output=123;
    assert(KlippyCodec::decode_u32(truncated,1,output).status==Status::NeedMoreData && output==123);
    const uint8_t too_long[]{0x80,0x80,0x80,0x80,0x80};
    assert(KlippyCodec::decode_u32(too_long,5,output).status==Status::TooLong && output==123);
    const uint8_t overflow[]{0x90,0x80,0x80,0x80,0x00};
    assert(KlippyCodec::decode_u32(overflow,5,output).status==Status::Overflow && output==123);
    assert(KlippyCodec::decode_u32(nullptr,1,output).status==Status::InvalidArgument);
    assert(KlippyCodec::decode_u32(nullptr,0,output).status==Status::NeedMoreData);
    uint8_t untouched=0xAA;
    assert(KlippyCodec::encode_u32(300,&untouched,1).status==Status::BufferTooSmall && untouched==0xAA);
    assert(KlippyCodec::encode_u32(0,nullptr,1).status==Status::InvalidArgument);
    // 결정적난수로signed/unsigned모두다양한비트패턴을검사한다.
    uint32_t seed=1;
    for (unsigned i=0;i<20000;++i) {
        seed=seed*1664525u+1013904223u;
        const auto e=KlippyCodec::encode_u32(seed,data,5);
        assert(e.status==Status::Ok);
        uint32_t decoded=0;
        assert(KlippyCodec::decode_u32(data,e.bytes_used,decoded).status==Status::Ok && decoded==seed);
        const int32_t signed_value=static_cast<int32_t>(static_cast<int64_t>(seed)+INT32_MIN);
        const auto s=KlippyCodec::encode_i32(signed_value,data,5);
        int32_t signed_decoded=0;
        assert(KlippyCodec::decode_i32(data,s.bytes_used,signed_decoded).status==Status::Ok && signed_decoded==signed_value);
    }
    puts("PASS: Klippy signed/unsigned VLQ boundaries, errors, 40000 roundtrips");
}
