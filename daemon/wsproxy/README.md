# wsproxy

This is the websocket to telnet bbs proxy.

## Dependency

- [robertabcd/openresty-debian](https://github.com/robertabcd/openresty-debian) -- Forked version to build [OpenResty](https://openresty.org) with patch.
- [Docker](https://www.docker.com/) -- Used to build OpenResty.
- [vstruct](https://github.com/toxicfrog/vstruct/) -- Lua library for binary manipulation.

## Install

- Build OpenResty with patch.
```
$ git clone https://github.com/robertabcd/openresty-debian
$ cd openresty-debian
$ git checkout ptt
$ ./build
# dpkg -i artifacts/*.deb
```

- Download vstruct
```
$ cd ~bbs/pttbbs/daemon/wsproxy && mkdir lib && cd lib
$ git clone https://github.com/toxicfrog/vstruct/
```

- Configure nginx
```nginx
lua_package_path ";;/home/bbs/pttbbs/daemon/wsproxy/lib/?.lua;/home/bbs/pttbbs/daemon/wsproxy/lib/?/init.lua";
server {
        listen 80 default_server;
        location /bbs {
                #set $bbs_secure 1;
                content_by_lua_file /home/bbs/pttbbs/daemon/wsproxy/wsproxy.lua;
        }
}
```

- Configure wsproxy.lua (interesting variables listed)
```lua
local logind_addr = "unix:/home/bbs/run/logind.connfwd.sock"

local origin_whitelist = {
    ["http://www.ptt.cc"] = true,
    ["https://www.ptt.cc"] = true,
    ["https://robertabcd.github.io"] = true,
    ["app://pcman"] = true,
}
```

- Configure logind (etc/bindports.conf)
```
logind unix run/logind.connfwd.sock
```
