# 3D Thumbnail

Windows 탐색기에서 3D 모델의 형상을 썸네일로 보여주는 Windows x64 프로그램입니다. **0.5.0 시험 배포**이며, 배경·모델 색상 설정과 사용자별 설치·제거를 지원합니다.

제작: **DMIND** / **dmind114@gmail.com** · GitHub: [XomPM](https://github.com/XomPM)

## 지원 형식

| 형식 | 확장자 | 처리 방식 |
| --- | --- | --- |
| STL | `.stl` | ASCII / 바이너리 메시 |
| 3MF | `.3mf` | 모델·컴포넌트·빌드 배치 직접 읽기 |
| STEP | `.step`, `.stp` | CAD 곡면을 삼각형으로 변환 |
| OBJ | `.obj` | 메시 읽기 |
| glTF | `.glb`, `.gltf` | 내장 메시 및 glTF의 같은 폴더 아래 외부 버퍼 |
| PLY | `.ply` | 면이 있는 ASCII / 바이너리 메시 |
| IGES | `.iges`, `.igs` | 곡면 / 솔리드 변환 |
| FBX | `.fbx` | ASCII / 바이너리 메시 |

모델링 프로그램이나 전용 GPU 없이 Direct3D WARP로 렌더링합니다. 원본 파일과 기본 파일 열기 앱은 변경하지 않습니다. 텍스처와 원본 재질 대신 설정한 단색에 입체 명암을 적용합니다.

## 설치와 사용

1. [Releases](https://github.com/XomPM/3d-thumbnail/releases)에서 `3DThumbnail-…-x64-Setup.exe`를 내려받아 실행합니다.
2. 탐색기에서 큰 아이콘 또는 아주 큰 아이콘 보기를 선택합니다. 탐색기의 ‘아이콘은 항상 표시하고 미리 보기는 표시하지 않음’ 옵션이 켜져 있으면 꺼야 합니다.
3. 시작 메뉴 → **3D Thumbnail → 색상 설정**에서 배경·모델 색상을 선택하거나 `#RRGGBB`를 입력합니다. 미리보기를 확인하고 **색상 저장**을 누릅니다.
4. 이미 만들어진 썸네일은 **파일 선택 후 갱신** 또는 **폴더 갱신**으로 새 색상을 적용합니다. 폴더 갱신은 바로 아래 지원 파일 최대 1,000개를 처리하며 하위 폴더를 탐색하지 않습니다. 갱신 중단은 현재 파일 처리가 끝난 뒤 적용됩니다.

기본 색상은 어두운 회색 배경 `#2B2B2B`, 밝은 회색 모델 `#D6D6D6`입니다. 설정은 현재 Windows 사용자 계정에 저장되며 PNG 생성에도 적용됩니다.

현재 사용자 전용으로 `%LOCALAPPDATA%\Programs\3DThumbnail`에 설치합니다. 새 설치 파일을 실행하면 버전별 실행 파일을 추가하고 등록을 갱신합니다. Windows 설정 → 앱 → 설치된 앱 또는 시작 메뉴의 **제거**로 삭제할 수 있습니다. 최초 썸네일 등록 백업은 `%LOCALAPPDATA%\3DThumbnail\registration.json`에 남기며, 다른 앱이 변경한 값은 제거 시 덮어쓰지 않습니다. 설정과 복구 기록은 제거 후에도 보존합니다. Explorer가 사용 중인 DLL은 파일 정리 시 남을 수 있습니다.

개발용 `register-shell.ps1` 설치를 사용 중이라면 해당 스크립트로 먼저 제거한 뒤 Setup을 설치하세요. 독립된 복구 기록을 가진 두 설치를 겹치지 마세요.

## PNG 생성과 진단

빌드 폴더 또는 설치된 `versions/<build-id>` 폴더에서 실행합니다. 출력 PNG가 이미 있으면 덮어쓰지 않습니다.

```powershell
.\thumbnail-cli.exe model.step preview.png --size 512
.\thumbnail-cli.exe model.stl colored.png --background '#14283C' --model '#FF8000'
.\shell-probe.exe --refresh 'C:\models\model.step'
.\shell-probe.exe --diagnose .step
```

CLI의 색상 옵션은 해당 실행에만 적용됩니다. 설정 창에 저장한 기본값을 변경하지 않습니다. `shell-probe --direct`는 직접 처리기 호출, `--shell`은 실제 Windows 썸네일 추출을 확인합니다.

## 제한과 검증 범위

- Windows 11 x64에서 개발·검증합니다. Windows 10, ARM64, NAS, OneDrive와 다른 PC의 호환성은 아직 검증하지 않았습니다.
- 입력 64 MiB, 삼각형 250,000개, 작업자 15초, 커밋 메모리 1 GiB 제한입니다. 크거나 복잡한 모델은 썸네일 생성에 실패할 수 있습니다.
- 점군·선 전용 형상, Draco/meshopt 압축, GLB 외부 버퍼, 애니메이션·스키닝 결과 재현은 지원하지 않거나 검증하지 않았습니다.
- glTF 외부 버퍼는 선언된 상대 경로만 읽습니다. `.bin`만 바꾸면 Windows 캐시가 자동 갱신되지 않을 수 있으므로 직접 갱신하세요.
- 작은 연결 DLL이 Explorer에서 동작하며, 형식 파싱·렌더링은 별도 작업자에서 수행합니다. Job Object는 시간·메모리 제한이며 파일·네트워크 권한을 제한하는 보안 샌드박스는 아닙니다.
- 설치 파일은 코드 서명되지 않은 시험 배포입니다. 다른 깨끗한 Windows PC에서의 설치 검증은 남아 있습니다.

CTest는 색상 설정·렌더링·형식·작업자·COM 처리기를 검사합니다. 등록 테스트는 별도 GUID 레지스트리에서 복구·외부 변경 보존·업데이트 중단을 검사합니다. 실제 Windows 설치·표시 검증은 이 자동 테스트와 별도로 수행합니다. 상세 구조는 [탐색기 연동 문서](docs/shell-integration.md)를 참고하세요.

## 소스 빌드

Visual Studio 2022의 **C++를 사용한 데스크톱 개발**, Windows SDK, CMake 도구, PowerShell이 필요합니다. 저장소 루트에서 실행합니다.

```powershell
.\scripts\setup-dependencies.ps1
.\scripts\build-occt.ps1
.\scripts\build.ps1
.\tests\registration_tests.ps1
.\scripts\package.ps1
```

첫 OCCT 빌드는 시간이 걸립니다. 선택한 CAD 도구 모음과 전이 의존성을 빌드하며 외부 영상·VR·폰트·메모리 할당 라이브러리는 끕니다. 패키지 제작은 `runtime-dependencies.json`에 기록된 DLL만 포함하고 해시를 확인합니다. 테스트 파일과 개발용 DLL을 통째로 배포하지 않습니다.

`dependencies.lock.json`에 다운로드 버전·출처·SHA-256을 고정합니다. `samples`는 직접 생성한 테스트 모델이며 외부 업무 파일을 포함하지 않습니다. GitHub Actions 수동 빌드 예시는 `docs/windows-build-workflow.yml.example`에 있습니다. 필요할 때 `.github/workflows/build.yml`로 복사해 활성화할 수 있습니다.

## 라이선스

직접 작성한 코드는 [MIT](LICENSE)입니다. OCCT, Assimp, lib3mf, Microsoft 런타임 등 외부 구성 요소에는 별도 조건이 적용됩니다. [제3자 고지](THIRD_PARTY_NOTICES.md)와 `licenses/`를 함께 확인하세요. OCCT 소스 아카이브는 해당 바이너리 릴리스와 함께 제공합니다.
