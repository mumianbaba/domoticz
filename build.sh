
cd build && cmake .. -DUSE_PYTHON=NO  -DOPENSSL_ROOT_DIR=/usr/local/openssl -DOPENSSL_LIBRARIES=/usr/local/openssl/lib -DSQLITE_SHM_DIRECTORY="/dev/shm"  -DCMAKE_INSTALL_PREFIX=/work/007_zigbee_splatform/domoticz/yp-domo/domoticz/build/_install && make

