FROM gjcchain/builder
MAINTAINER GJChain


RUN export LC_ALL=C

RUN git config --global http.postBuffer 1048576000

RUN git clone https://github.com/GameJoyChain/gamejoychain.git
RUN cd gjc-core \
    && git checkout master \
    && git submodule update --init --recursive \
    && mkdir build && cd build \
    && export WASM_ROOT=~/opt/wasm \
    && export C_COMPILER=clang-4.0 \
    && export CXX_COMPILER=clang++-4.0 \
    && cmake -G "Unix Makefiles" -DWASM_ROOT=~/opt/wasm -DCMAKE_INSTALL_PREFIX=~/opt/wasm -DLLVM_TARGETS_TO_BUILD= -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=WebAssembly -DCMAKE_BUILD_TYPE=Release .. \
    && make witness_node cli_wallet \
    && cp -rf programs /usr/local/

# Home directory $HOME
WORKDIR /

ENV GJC_DATA /gjc
RUN rm -rf "$GJC_DATA" \
    && mkdir "$GJC_DATA" \
    && ln -sfn /usr/local/programs "$GJC_DATA"

VOLUME /gjc

# Copy default configuration
ADD docker/start.sh /start.sh
ADD genesis.json /genesis.json
RUN chmod a+x /start.sh

# rpc service port（cli_wallet or witness_node）
EXPOSE 8091

# P2P node to connect to on startup
EXPOSE 8092

# Endpoint for P2P node to listen on
EXPOSE 8093

# Entry point
ENTRYPOINT [ "/start.sh" ]
