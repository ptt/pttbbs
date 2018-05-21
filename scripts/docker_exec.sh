#!/bin/bash

sudo -u bbsadm /home/bbs/bin/shmctl init
sudo -u bbsadm /home/bbs/bin/mbbsd -p 3000 -d

sleep infinity
