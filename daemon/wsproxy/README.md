# wsproxy

The websocket to telnet bbs proxy.

## Dependency

- [vstruct](https://github.com/toxicfrog/vstruct/) -- Lua library for binary manipulation.

## Install

- Install OpenResty.

Follow the [installation instruction](https://openresty.org/en/installation.html) on the OpenResty
official website.

- Download vstruct

```
$ cd ~bbs/pttbbs/daemon/wsproxy && mkdir lib && cd lib
$ git clone https://github.com/toxicfrog/vstruct/
```

- Configure nginx

```nginx
lua_package_path ";;/home/bbs/pttbbs/daemon/wsproxy/lib/?.lua;/home/bbs/pttbbs/daemon/wsproxy/lib/?/init.lua";

map $http_origin $bbs_origin_checked {
    "http://www.ptt.cc" 1;
    "https://www.ptt.cc" 1;
    "~^app://" 1;
    "~^https?://127\." 1;
    default 0;
}

server {
    location /bbs {
        set $bbs_secure 1;
        set $bbs_logind_addr "unix:/home/bbs/run/logind.connfwd.sock";
        content_by_lua_file /home/bbs/wsproxy/wsproxy.lua;
    }
}
```

- Configure logind (etc/bindports.conf)

```
logind unix run/logind.connfwd.sock
```

## License

MIT
