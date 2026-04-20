# shared/config/.commands — OS-level command SSOT
# 포맷:
#   command <name> <category> "<description>"
#     keywords <k1> <k2> ...
#     handler <relative/path.hexa>
#     shim <yes|no>
# (빈 줄로 command 구분, # = 주석, column 0 = header, column 2 = key)

command drill core "F6 DRILL_GAP divergent seed 발사 (N>=3 angles)"
  keywords drill 드릴
  handler tool/cmd_drill.hexa
  shim yes

command go core "loop 엔진 풀사이클"
  keywords go 가자
  handler tool/cmd_go.hexa
  shim yes

command todo core "전 프로젝트 할일 표 출력"
  keywords todo 할일
  handler tool/cmd_todo.hexa
  shim yes

command doctor core "OS-level 강제 환경 자가 진단"
  keywords doctor 진단
  handler tool/doctor.hexa
  shim yes

command install-os core "1회 부트스트랩: shim/PATH/launchd/chflags"
  keywords install-os install
  handler tool/install_os.hexa
  shim no

command watchd daemon "launchd 데몬 — 1s polling 으로 shim 동기화"
  keywords watchd
  handler tool/watchd.hexa
  shim no

command smash bridge "blowup 엔진 smash"
  keywords smash
  handler tool/cmd_smash.hexa
  shim yes

command free bridge "blowup 엔진 free_dfs"
  keywords free
  handler tool/cmd_free.hexa
  shim yes

command loop core "loop harness 재실행"
  keywords loop
  handler tool/cmd_loop.hexa
  shim yes

command roi core "ROI 표 출력"
  keywords roi
  handler tool/cmd_roi.hexa
  shim yes

command keep core "keep-alive session"
  keywords keep
  handler tool/cmd_keep.hexa
  shim yes

command list core "커맨드 목록 출력"
  keywords list ls
  handler tool/cmd_list.hexa
  shim yes
