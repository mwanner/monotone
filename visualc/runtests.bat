SETLOCAL

set EXE_PATH=visualc\Release
set PATH=%CD%\%EXE_PATH%;%PATH%

set fail_count=0

unit_tests.exe
if %ERRORLEVEL%==0 goto tester-testsuite.lua
@echo unit-tests failed.
set fail_count=1

:tester-testsuite.lua
%EXE_PATH%\tester.exe tester-testsuite.lua
if %ERRORLEVEL%==0 goto testsuite.lua
@echo tester-testsuite.lua failed.
if %fail_count%==1 set fail_count=2
if %fail_count%==0 set fail_count=1

:testsuite.lua
%EXE_PATH%\tester.exe testsuite.lua
if %ERRORLEVEL%==0 goto nomore
@echo testsuite.lua failed.
if %fail_count%==2 set fail_count=3
if %fail_count%==1 set fail_count=2
if %fail_count%==0 set fail_count=1

:nomore
if %fail_count%==0 goto end

@rem return an error code
@echo ===============================
@echo %fail_count% of 3 tests failed.
@echo ===============================
exit /B 1

:end
@echo ===============================
@echo All tests succeeded!
@echo ===============================