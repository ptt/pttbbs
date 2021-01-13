# wsproxy

The websocket to telnet bbs proxy.

## Install

- Install OpenResty.

Follow the [installation instruction](https://openresty.org/en/installation.html) on the OpenResty
official website.

- Configure nginx

```nginx
map $http_origin $bbs_origin_checked {
    "http://www.ptt.cc" 1;
    "https://www.ptt.cc" 1;
    "~^app://" 1;
    "~^https?://127\." 1;
    default 0;
}

server {
    location /bbs {
        # Optional: lport sent to logind.
        # Values: an integer.
        # Default: server port.
        set $bbs_lport 443;

        # Optional: Whether it is a secure connection.
        # Values: 1 for secure, otherwise non-secure.
        # Default: non-secure.
        set $bbs_secure 1;

        # Required: The logind address.
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
