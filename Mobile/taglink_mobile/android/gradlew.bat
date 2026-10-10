@echo off
setlocal
set GRADLE_VERSION=8.7
set GRADLE_HOME=%USERPROFILE%\.gradle\taglink-gradle-%GRADLE_VERSION%
if exist "%GRADLE_HOME%\bin\gradle.bat" goto runGradle
where gradle >nul 2>nul
if %ERRORLEVEL%==0 goto systemGradle
set ZIP=%TEMP%\gradle-%GRADLE_VERSION%-bin.zip
set STAGE=%TEMP%\taglink-gradle-%GRADLE_VERSION%
powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -Uri 'https://services.gradle.org/distributions/gradle-%GRADLE_VERSION%-bin.zip' -OutFile '%ZIP%'"
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%STAGE%" mkdir "%STAGE%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -Force '%ZIP%' '%STAGE%'"
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%USERPROFILE%\.gradle" mkdir "%USERPROFILE%\.gradle"
move "%STAGE%\gradle-%GRADLE_VERSION%" "%GRADLE_HOME%" >nul
:runGradle
call "%GRADLE_HOME%\bin\gradle.bat" %*
exit /b %ERRORLEVEL%
:systemGradle
gradle %*
exit /b %ERRORLEVEL%
