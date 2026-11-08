"""실제 C++ 코덱과 기존 Klippy Python의 VLQ 결과를 PC에서 교차 확인한다.

참조 msgproto.py는 시험용으로만 읽는다. 제품 FW에 포함·링크하지 않는다.
RPi 실행 중인 커밋 자체를 검사하는 것이 아니라 지정한 로컬 참조 파일을 검사한다.
"""
import ctypes
import importlib.util
from pathlib import Path
import random
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
reference=root.parent/'reference/klippy/msgproto.py'
build=root/'build'
build.mkdir(exist_ok=True)
bridge=build/'klippy-codec-bridge.cpp'
bridge.write_text('''
#include "klippy_codec.h"
extern "C" unsigned encode_value(int is_signed, long long value, unsigned char* bytes) {
    const auto result = is_signed
        ? KlippyCodec::encode_i32(static_cast<int32_t>(value),bytes,5)
        : KlippyCodec::encode_u32(static_cast<uint32_t>(value),bytes,5);
    return result.status == KlippyCodec::Status::Ok ? result.bytes_used : 0;
}
extern "C" unsigned decode_value(int is_signed, const unsigned char* bytes, unsigned length, long long* value) {
    if (is_signed) {
        int32_t decoded=0;
        const auto result=KlippyCodec::decode_i32(bytes,length,decoded);
        *value=decoded;
        return result.status == KlippyCodec::Status::Ok ? result.bytes_used : 0;
    }
    uint32_t decoded=0;
    const auto result=KlippyCodec::decode_u32(bytes,length,decoded);
    *value=decoded;
    return result.status == KlippyCodec::Status::Ok ? result.bytes_used : 0;
}
''',encoding='utf-8')
subprocess.run(['g++','-std=c++17','-shared','-fPIC','-O2','-I'+str(root/'klippy'),
                str(root/'klippy/klippy_codec.cpp'),str(bridge),'-o',str(build/'klippy-codec.so')],check=True)
lib=ctypes.CDLL(str(build/'klippy-codec.so'))
pointer=ctypes.POINTER(ctypes.c_ubyte)
lib.encode_value.argtypes=[ctypes.c_int,ctypes.c_longlong,pointer]
lib.encode_value.restype=ctypes.c_uint
lib.decode_value.argtypes=[ctypes.c_int,pointer,ctypes.c_uint,ctypes.POINTER(ctypes.c_longlong)]
lib.decode_value.restype=ctypes.c_uint
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('reference_msgproto',reference)
ref=importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref)
rng=random.Random(0x500)
count=0
for signed in (False,True):
    minimum=-(1<<31) if signed else 0
    maximum=(1<<31)-1 if signed else (1<<32)-1
    values={minimum,maximum,0,1,95,96,127,128,300}
    for boundary in (-67108864,-524288,-4096,-32,96,12288,1572864,201326592):
        values.update(n for n in (boundary-1,boundary,boundary+1) if minimum<=n<=maximum)
    values.update(rng.randint(minimum,maximum) for _ in range(5000))
    codec=ref.PT_int32() if signed else ref.PT_uint32()
    for value in values:
        expected=[]
        codec.encode(expected,value)
        actual=(ctypes.c_ubyte*5)()
        length=lib.encode_value(signed,value,actual)
        assert bytes(actual[:length])==bytes(expected),(signed,value,expected,actual[:length])
        decoded=ctypes.c_longlong()
        used=lib.decode_value(signed,actual,length,ctypes.byref(decoded))
        assert used==length and decoded.value==value
        reference_value,reference_used=codec.parse(expected,0)
        assert reference_value==value and reference_used==used
        count+=1
print(f'PASS: {count} signed/unsigned values match local Klippy; reference={reference}')
