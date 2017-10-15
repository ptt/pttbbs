local server = require "resty.websocket.server"
local vstruct = require "vstruct"

local logind_addr = "unix:/home/bbs/run/logind.connfwd.sock"

local origin_whitelist = {
    ["http://www.ptt.cc"] = true,
    ["https://www.ptt.cc"] = true,
    ["https://robertabcd.github.io"] = true,
    ["app://pcman"] = true,
}

function check_origin()
    local origin = ngx.req.get_headers().origin
    if type(origin) ~= "string" then
        ngx.log(ngx.ERR, "only single origin expected, got: ", origin)
        return ngx.exit(400)
    end
    if not origin_whitelist[origin] then
        ngx.log(ngx.ERR, "origin not whitelisted: ", origin)
        return ngx.exit(403)
    end
end

function build_conn_data()
    local fmt = vstruct.compile("< u4 u4 u4 s16 u2 u2 u4")
    local flags = 0
    local secure = tonumber(ngx.var.bbs_secure) or 0
    if secure == 1 then
	flags = flags + 1 -- CONN_FLAG_SECURE
    end
    return fmt:write({
        36, -- size
        0,  -- encoding
        ngx.var.binary_remote_addr:len(),   -- len_ip
        ngx.var.binary_remote_addr,         -- ip16
        tonumber(ngx.var.remote_port) or 0, -- rport
        tonumber(ngx.var.server_port) or 0, -- lport
	flags,
    })
end

function connect_mbbsd()
    local mbbsd = ngx.socket.stream()
    local ok, err = mbbsd:connect(logind_addr)
    if not ok then
        ngx.log(ngx.ERR, "failed to connect to mbbsd: ", err)
        return ngx.exit(555)
    end

    local _, err = mbbsd:send(build_conn_data())
    if err then
        ngx.log(ngx.ERR, "failed to send conn data to mbbsd: ", err)
        return ngx.exit(555)
    end

    return mbbsd
end

function start_websocket_server()
    local ws, err = server:new({
        timeout = 30*60*1000,  -- in milliseconds
        max_payload_len = 65535,
    })
    if not ws then
        ngx.log(ngx.ERR, "failed to new websocket: ", err)
        return ngx.exit(444)
    end
    return ws
end

function ws2sock(ws, sock)
    last_typ = ""
    while true do
        local data, typ, err = ws:recv_frame()
        if err or not data then
            ngx.log(ngx.ERR, "failed to receive a frame: ", err)
            return ngx.exit(444)
        end

        if typ == "continuation" then
            typ = last_typ
        end

        if typ == "binary" then
            _, err = sock:send(data)
            if err then
                ngx.log(ngx.ERR, "failed to send to mbbsd: ", err)
                return ngx.exit(555)
            end
        elseif typ == "close" then
            sock:close()
            local _, err = ws:send_close(1000, "bye")
            if err then
                ngx.log(ngx.ERR, "failed to send the close frame: ", err)
                return
            end
            local code = err
            ngx.log(ngx.INFO, "closing with status code ", code, " and message ", data)
            return
        elseif typ == "ping" then
            -- send a pong frame back:
            local _, err = ws:send_pong(data)
            if err then
                ngx.log(ngx.ERR, "failed to send frame: ", err)
                return
            end
        elseif typ == "pong" then
            -- just discard the incoming pong frame
        else
            ngx.log(ngx.INFO, "received a frame of type ", typ, " and payload ", data)
        end

        last_typ = typ
    end
end

function sock2ws(sock, ws)
    while true do
        sock:settimeout(30*60*1000)
        data, err, partial = sock:receiveatmost(1024)
        if partial then
            data = partial
        end

        if not data then
            ws:send_close(1000, "bbs died")
            ngx.log(ngx.ERR, "failed to recv from mbbsd: ", err)
            return ngx.exit(444)
        else
            bytes, err = ws:send_binary(data)
            if not bytes then
                ngx.log(ngx.ERR, "failed to send a binary frame: ", err)
                return ngx.exit(444)
            end
        end
    end
end

check_origin()
local ws = start_websocket_server()
local sock = connect_mbbsd()
ngx.log(ngx.ERR, "client connect over websocket, ",
    ngx.var.server_name, ":", ngx.var.server_port, " ", ngx.var.server_protocol)
ngx.thread.spawn(ws2sock, ws, sock)
ngx.thread.spawn(sock2ws, sock, ws)
