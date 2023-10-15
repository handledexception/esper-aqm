@ECHO OFF
idf.py build
idf.py flash -p COM3 -b 115200
idf.py monitor -p COM3 -B 115200
