# 원본 테스트 샘플

모든 모델은 이 프로젝트를 위해 직접 생성했습니다. 외부 모델을 복사하지 않았으며 실제 제조용 설계로 검증하지 않았습니다.

| 파일 | 내용 |
| --- | --- |
| bearing_binary.stl, bearing_ascii.stl | 사각 받침대와 속이 빈 원통, 524개 삼각형, 법선 0 |
| bearing.obj | 같은 메시를 음수 인덱스로 표현; 외부 재질 없음 |
| bearing.3mf | 같은 메시, 내장 썸네일 없음, 정상 build 항목 |
| nested_instances.3mf | 사면체의 중첩 이동→회전→이동과 두 인스턴스; 미사용 자원 포함 |
| empty_build.3mf | 의도적으로 빈 build인 실패 검증용 파일 |
| bearing_ap203.stp, bearing_ap214.step, bearing_ap242.step | OCCT로 직접 만든 곡면 CAD 형상을 각각 STEP 규격으로 저장 |
| placed_solids.step | 같은 STEP 부품을 (100, 20, 0) 이동해 두 개 배치 |

단일 베어링의 범위는 x=-30..30, y=-22..22, z=0..30입니다. 두 STEP 부품의 전체 범위는 x=-30..130, y=-22..42, z=0..30입니다. STEP 삼각형 수는 전체 크기에 따른 곡면 근사 정밀도로 결정되므로 STL과 같지 않습니다.

중첩 3MF는 두 사면체의 총 8개 삼각형이며 범위는 x=98..150, y=10..12, z=0..2입니다. 멀리 있는 미사용 자원은 결과에 포함되면 안 됩니다.

프로젝트 루트에서 재생성합니다. Python은 샘플 재생성에만 필요합니다. STEP 작성기는 개발 빌드에 포함됩니다.

~~~powershell
python .\scripts\make_sample.py
.\build\Release\make-step-samples.exe .\samples
~~~

STEP/IGES 헤더에는 생성 시각이 포함되므로 재생성 후 바이트 해시는 달라질 수 있습니다. 기하 검증 기준은 고정된 범위·부품 배치입니다. 생성에 쓴 라이브러리와 같은 라이브러리로 읽는 검증이므로 다른 CAD 제작 도구의 실제 파일 검증을 대신하지 않습니다.

0.4.0: `python scripts/make_mesh_samples.py`는 원본 bearing OBJ에서 PLY ASCII/바이너리, GLB, 내장·외부 버퍼 glTF, 두 부품 GLB, FBX ASCII/바이너리를 만듭니다. `bearing.bin`은 `bearing.gltf`의 필수 데이터입니다. glTF는 Y-up으로 저장하며 로더에서 Z-up으로 변환한 뒤 기존 범위를 검사합니다. FBX 바이너리는 7.4 배열 압축을 포함합니다. `make-step-samples`는 `bearing.iges`(면)와 `bearing_brep.igs`(BRep)도 만듭니다. 모든 추가 샘플은 이 프로젝트가 직접 작성한 형상입니다.
