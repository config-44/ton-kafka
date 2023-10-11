## TON C++ monorepo with Kafka feature setup manual

1. Install dependencies from apt
- `sudo apt update`
- `sudo apt upgrade`
- `sudo apt install build-essential clang openssl libssl-dev zlib1g-dev gperf libreadline-dev ccache libmicrohttpd-dev pkg-config libssl-dev jq`

2. Install `librdkafka-v2.0.2` from sources
- `wget https://github.com/confluentinc/librdkafka/archive/refs/tags/v2.0.2.tar.gz`
- `tar zxvf v2.0.2.tar.gz`
- `rm v2.0.2.tar.gz`
- `cd librdkafka-2.0.2/`
- `./configure --install-deps`
- `make`
- `sudo make install`

3. Install `cmake v3.20.0` from sources
- `git clone https://gitlab.kitware.com/cmake/cmake.git`
- `cd cmake`
- `git checkout v3.20.0`
- `./bootstrap`
- `make`
- `sudo make install`

4. Check cmake version
- `cmake --version` (this should output `cmake version 3.20.0`...)

5. Install boost library 
- `wget https://boostorg.jfrog.io/artifactory/main/release/1.83.0/source/boost_1_83_0.tar.gz`
- `tar zxvf boost_1_83_0.tar.gz`
- `cd boost_1_83_0/`
- `./bootstrap.sh --prefix=/usr/`
- `sudo ./b2 install`

6. Compile `validator-engine` (in ton monorepo directory) 
(don't forget `--recurse-submodules` when cloning the repository)
- `mkdir build`
- `cd build`
- `export CC=gcc`
- `export CXX=g++`
- `cmake -DCMAKE_BUILD_TYPE=Release ..`
- `cmake --build . --target validator-engine`

7. Check `validator-engine` binary 
- `./validator-engine/validator-engine -V`

8. Run `validator-engine` service

Run this command with flags (replace flags with your own, it's just an example) 
- `validator-engine -C my-ton-global.config.json --db $HOME/Programs/ton-node/db --ip 0.0.0.0:18411 -l $HOME/Programs/ton-node/log -t 1 -v 3`

`-C` – path to blockchain global config (can be downloaded from https://ton-blockchain.github.io/global.config.json for ton mainnet and https://ton-blockchain.github.io/testnet-global.config.json for ton testnet)
`--db` – path to database directory
`--ip` – external ip address for validator service
`-l` - logs filebase, in example `log` a the ent of the path it's not a direcoty or file, just 'filebase'
`-t` – threads, for debug you can set `-t 1`, for production use thread per core
`-v` – logging verbosity lvl, in most cases `-v 3` is enough

Then convert Kafka service IP into integer format. For example if kafka ip address is `127.0.0.1`, run this command `echo 127.0.0.1 | tr . '\n' | awk '{s = s*256 + $1} END{print s}'`, and the result will be `2130706433`. Also you can use https://www.vultr.com/resources/ipv4-converter/ to complete it in the web interface.

After running the `validator-engine ...` command you may have noticed that nothing happened. But in fact a `config.json` file has been created in the database directory, you can check it to make sure everything went well, but we also have to set the parameters there for running with Kafka. Execute this commands, but replace `INT_IP` with kafka integer ip address and `INT_PORT` with kafka netowrk port. Also replace `config.json` with exact path to `config.json` file in database directory if needed.

- `jq '.kafka_config.ip = "INT_IP"' config.json > tmp.json && cat tmp.json > config.json`
- `jq '.kafka_config.port = "INT_PORT"' config.json > tmp.json && cat tmp.json > config.json`

You can also change the other fields such as `.kafka_config.timeout_ms`, `.kafka_config.max_bytes` and `.kafka_config.topics.blocks` to your own values in the same way.

Then run the `validator-engine ...` with the same parameters again and and the full node should start running.
