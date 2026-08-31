FROM ubuntu:22.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    g++ make cmake pkg-config libmysqlclient-dev libssl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN make server

EXPOSE 9007
CMD ["./server", "-p", "9007", "-d", "0", "-S", "0", "-H", "60"]
