Quick Installation
==========

Install System dpkgs:
-----
```
apt-get -y git pmake clang ccache libevent-dev
```

Use root
-----
Goal:
* Add bbs and bbsadm as user with same uid (9999).
* group-name: bbs
* gid: 99
* home directory: /home/bbs
* shell for bbsadm: /bin/sh (or /bin/bash, /bin/csh, etc)
* shell for bbs: /home/bbs/bin/bbsrf
* bbsadm: with password
* bbs: no password

You can use adduser and related commands to achieve the goal.


Example for Debian / Ubuntu:

1. vipw and add the following two lines:

    ```
    bbs::9999:99:PttBBS,,,:/home/bbs:/home/bbs/bin/bbsrf
    bbsadm::9999:99:PttBBS,,,:/home/bbs:/bin/bash
    ```

2. `passwd bbsadm`

3. add the following line in /etc/group:

    ```
    bbs:x:99:
    ```

4. `mkdir -p /home/bbs`

5. `chown -R bbs:bbs /home/bbs`

6. `chmod 700 /home/bbs`


Use bbsadm
-----
In /home/bbs:

7. `git clone http://github.com/ptt/pttbbs`

8. `cd /home/bbs/pttbbs`

9. `cp sample/pttbbs.conf pttbbs.conf`

10. Change pttbbs.conf based on your need.
    Uncomment the following two lines if you are in 64-bit OS:

    ```    
    #define SHMALIGNEDSIZE (1048576*4)
    #define TIMET64
    ```

11. (If you are in linux: `alias make=pmake`)

    ```
    make BBSHOME=/home/bbs all install
    ```

12. If you are setting up a new site:

    ```
    cd sample; make install
    cd /home/bbs; bin/initbbs -DoIt
    ```
    
Start BBS
-----
15. `/home/bbs/bin/shmctl init`

16. Use root to do the following:

    ```
    /home/bbs/bin/mbbsd -p 23 -d
    ```
    
17. Test with the following:

    ```
    telnet localhost 23
    ```
    
18. new a bbs-account named SYSOP

19. logout and then re-login as SYSOP, then you have SYSOP privilege.
