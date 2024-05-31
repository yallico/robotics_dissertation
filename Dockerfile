FROM espressif/idf:latest

WORKDIR /swarmcom

COPY . /swarmcom

CMD ["/bin/bash"]
