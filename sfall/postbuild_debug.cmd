@ECHO OFF

rem debug, release, etc.
SET type=%1
rem full path to the compiled DLL
SET target=%2

SET destination="F:\Fallout2\ddraw.dll"

echo Copying %target% to %destination% ...
copy %target% %destination%