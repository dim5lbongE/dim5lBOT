# dim5lBOT Updater for Windows

Windows 10/11용 dim5lBOT 자동 업데이터입니다. 관리자 권한 없이 실행되며 Geode의 `mods` 폴더를 자동 탐색합니다.

## 안전한 교체 방식

1. GitHub의 `updates/latest.json` 확인
2. `.geode` 파일을 동일 폴더의 임시 파일로 다운로드
3. SHA-256, `mod.json` ID/버전, Windows 지원 여부 검사
4. 기존 파일 백업 후 원자적으로 교체
5. 설치 파일을 다시 SHA-256 검증하고 실패 시 복구

## 빌드

```powershell
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```
