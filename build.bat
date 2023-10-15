@ECHO OFF
idf.py build
idf.py flash -p COM5 -b 115200
idf.py monitor -p COM5 -B 115200
