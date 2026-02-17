@echo off
set MSYSTEM=
set MSYS=
set CHERE_INVOKING=
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5
set IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env
set ESP_IDF_VERSION=5.5
set PATH=C:\Espressif\python_env\idf5.5_py3.11_env\Scripts;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119\riscv32-esp-elf\bin;C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin;C:\Espressif\tools\idf-python\3.11.2;C:\Windows\system32;C:\Windows
cd /d C:\ESP32-P4-WIFI6-Touch-LCD-7B\field-tool
python %IDF_PATH%\tools\idf.py build
