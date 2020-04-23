
cd build && cmake .. -DUSE_PYTHON=NO  -DOPENSSL_ROOT_DIR=/usr/local/openssl -DOPENSSL_LIBRARIES=/usr/local/openssl/lib   -DCMAKE_INSTALL_PREFIX=$(pwd)/_install && make

