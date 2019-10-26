--[[
wsproxy: the websocket to telnet bbs proxy

Copyright (c) 2017 Robert Wang <robertabcd@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
--]]

local server = require "resty.websocket.server"
local vstruct = require "vstruct"

local timeout_ms = 7*24*60*60*1000
local bbs_receive_size = 1024

local check_origin = function ()
    local checked = tonumber(ngx.var.bbs_origin_checked)
    if checked ~= 1 then
        ngx.log(ngx.ERR, "origin checked failed: ", ngx.req.get_headers().origin)
        return ngx.exit(403)
    end
end

local build_conn_data = function ()
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

local connect_mbbsd = function ()
    local addr = ngx.var.bbs_logind_addr
    if not addr then
        ngx.log(ngx.ERR, "bbs_logind_addr not set")
        return ngx.exit(500)
    end

    local mbbsd = ngx.socket.stream()
    local ok, err = mbbsd:connect(addr)
    if not ok then
        ngx.log(ngx.ERR, "failed to connect to mbbsd: ", addr, " err: ", err)
        return ngx.exit(500)
    end

    local _, err = mbbsd:send(build_conn_data())
    if err then
        ngx.log(ngx.ERR, "failed to send conn data to mbbsd: ", err)
        return ngx.exit(500)
    end

    return mbbsd
end

local start_websocket_server = function ()
    local ws, err = server:new({
        timeout = timeout_ms,
        max_payload_len = 65535,
    })
    if not ws then
        ngx.log(ngx.ERR, "failed to new websocket: ", err)
        return ngx.exit(400)
    end
    return ws
end

local ws2sock = function (ws, sock)
    local last_typ = ""
    while true do
        local data, typ, err = ws:recv_frame()
        if err or not data then
            ngx.log(ngx.ERR, "failed to receive a frame: ", err)
            return ngx.exit(400)
        end

        if typ == "continuation" then
            typ = last_typ
        end

        if typ == "binary" then
            local _, err = sock:send(data)
            if err then
                ngx.log(ngx.ERR, "failed to send to mbbsd: ", err)
                return ngx.exit(500)
            end
        elseif typ == "close" then
            sock:close()
            local _, err = ws:send_close(1000, "bye")
            if err then
                ngx.log(ngx.ERR, "failed to send the close frame: ", err)
                return
            end
            ngx.log(ngx.INFO, "closing with err ", err, " and message ", data)
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

local sock2ws = function (sock, ws)
    while true do
        sock:settimeout(timeout_ms)
        local data, err = sock:receiveany(bbs_receive_size)
        if not data then
            local bytes, send_err = ws:send_close(1000, "bbs died")
            ngx.log(ngx.ERR, "send_close: ", err, " bytes: ", bytes)
            ngx.log(ngx.ERR, "failed to recv from mbbsd: ", err)
            return ngx.exit(400)
        else
            ngx.log(ngx.DEBUG, "receive bytes from mbbsd: len: ", data:len())
            local bytes, err = ws:send_binary(data)
            if not bytes then
                ngx.log(ngx.ERR, "failed to send a binary frame: ", err)
                return ngx.exit(400)
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
