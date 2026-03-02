@echo off
set REXSDK=D:\recomp\360\ctxbla\tools\rexglue-sdk\out\install\win-amd64
cd /d "%~dp0"
out\build\win-amd64-release\comixzone.exe game_files --log_file=comixzone.log --log_level=info %*
