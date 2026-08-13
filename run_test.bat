@echo off
build\landwar.exe --headless --seed 42 --ticks 50 --summary > out_capture.txt 2>&1
echo ERRLEVEL=%errorlevel%
