FROM ubuntu:16.04

RUN apt-get update && apt-get install -y wget curl git openssh-server vim sudo apt-transport-https

# mongodb
RUN apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv 2930ADAE8CAF5059EE73BB4B58712A2291FA4AD5
RUN echo "deb [ arch=amd64,arm64 ] https://repo.mongodb.org/apt/ubuntu xenial/mongodb-org/3.6 multiverse" | tee /etc/apt/sources.list.d/mongodb-org-3.6.list

RUN apt-get update && apt-get install -y bmake ccache clang libevent-dev pkg-config

RUN apt-get install -y mongodb-org

# mongo-c-driver
RUN wget https://github.com/mongodb/mongo-c-driver/releases/download/1.9.4/mongo-c-driver-1.9.4.tar.gz

RUN tar -zxvf mongo-c-driver-1.9.4.tar.gz

RUN cd mongo-c-driver-1.9.4; ./configure; make; make install;

# libbson
RUN wget https://github.com/mongodb/libbson/releases/download/1.9.4/libbson-1.9.4.tar.gz

RUN tar -zxvf libbson-1.9.4.tar.gz

RUN cd libbson-1.9.4; ./configure; make; make install;

RUN echo "bbs:x:9999:99:PttBBS,,,:/home/bbs:/home/bbs/bin/bbsrf" >> /etc/passwd

RUN echo "bbsadm::9999:99:PttBBS,,,:/home/bbs:/bin/bash" >> /etc/passwd

RUN echo "bbsadm:[TO_BE_REPLACED]" | chpasswd

RUN echo "bbs:x:99:" >> /etc/group

RUN mkdir -p /home/bbs/pttbbs

RUN chown -R bbs:bbs /home/bbs

RUN chmod 700 /home/bbs

COPY common /home/bbs/pttbbs/common

COPY daemon /home/bbs/pttbbs/daemon

COPY include /home/bbs/pttbbs/include

COPY mbbsd /home/bbs/pttbbs/mbbsd

COPY pttbbs.conf /home/bbs/pttbbs/pttbbs.conf

COPY pttbbs.mk /home/bbs/pttbbs/pttbbs.mk

COPY pttbbs_cond.mk /home/bbs/pttbbs/pttbbs_cond.mk

COPY pttbbs_mbbsd.mk /home/bbs/pttbbs/pttbbs_mbbsd.mk

COPY trans /home/bbs/pttbbs/trans

COPY upgrade /home/bbs/pttbbs/upgrade

COPY util /home/bbs/pttbbs/util

COPY scripts /home/bbs/pttbbs/scripts

COPY Makefile.notest /home/bbs/pttbbs/Makefile

RUN cd /home/bbs/pttbbs; pmake clean; pmake all BBSHOME=/home/bbs; pmake install BBSHOME=/home/bbs;

WORKDIR /home/bbs

RUN chown -R bbsadm:bbs /home/bbs

RUN ldconfig

RUN /usr/bin/mongod --fork --config /etc/mongod.conf

RUN sudo -u bbsadm /home/bbs/bin/initbbs -DoIt

CMD ["/home/bbs/pttbbs/scripts/docker_exec.sh"]
