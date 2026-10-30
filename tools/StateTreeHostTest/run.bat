@echo off
rem Builds the state tree core with host stubs (no server, no boost) and runs the tree checks: run.bat [vcvars64.bat]
setlocal
set VCVARS=%~1
if "%VCVARS%"=="" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul 2>nul

set H=%~dp0
set H=%H:~0,-1%
set R=%H%\..\..
set S=%R%\playerbot\StateTree
if not exist "%H%\out" mkdir "%H%\out"

python "%H%\gen_stubs.py" || exit /b 1

pushd "%H%\out"
cl /nologo /std:c++20 /EHsc /permissive- /Zc:__cplusplus /Zc:preprocessor /W3 /wd4996 /wd4244 /wd4267 /wd4305 /Od /DMANGOSBOT_ZERO ^
  /I "%H%\stubs" /I "%R%" /I "%H%" /FI "%H%\stubs\stubpch.h" ^
  "%S%\STAI_Types.cpp" "%S%\STAI_TaskFactory.cpp" "%S%\STAI_Tree.cpp" "%S%\STAI_TreeInstance.cpp" "%S%\STAI_ActivityLog.cpp" ^
  "%S%\Tasks\STAI_Task.cpp" "%S%\Tasks\STAI_SendEventTask.cpp" "%S%\Tasks\Task_Wait.cpp" "%S%\Tasks\Task_RunSubtree.cpp" ^
  "%S%\Conditions\Condition_Compare.cpp" "%S%\Conditions\STAI_BuiltinConditions.cpp" "%S%\Considerations\STAI_BuiltinConsiderations.cpp" ^
  "%H%\harness_main.cpp" "%H%\out\stub_tasks.cpp" /Fe:"%H%\out\sthost.exe" > "%H%\out\build.log" 2>&1
set RC=%errorlevel%
popd
if not "%RC%"=="0" (
  findstr /C:"error" "%H%\out\build.log"
  echo build failed, see %H%\out\build.log
  exit /b %RC%
)

"%H%\out\sthost.exe" "%H%\tests" "%R%"
