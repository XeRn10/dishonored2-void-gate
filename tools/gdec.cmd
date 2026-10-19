@echo off
rem Run tools\gdec.py with the pyghidra env (Python 3.12 from the pyghidra-mcp uv tool) and JDK 21.
set "GHIDRA_INSTALL_DIR=D:\RE\ghidra_12.1.4_PUBLIC"
set "JAVA_HOME=D:\RE\jdk-21.0.12.1+1"
set "PYTHONIOENCODING=utf-8"
"%APPDATA%\uv\tools\pyghidra-mcp\Scripts\python.exe" "%~dp0gdec.py" %*
