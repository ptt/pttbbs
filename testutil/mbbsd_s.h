#pragma once

#define MAX_BINDPORT 20
enum TermMode {
    TermMode_TELNET,
    TermMode_TTY,
};

struct ProgramOption {
    bool	daemon_mode;
    bool	tunnel_mode;
    enum TermMode term_mode;
    int		term_width, term_height;
    int		nport;
    int		port[MAX_BINDPORT];
    int		flag_listenfd;
    char*	flag_tunnel_path;

    bool	flag_fork;
    bool	flag_checkload;
    char	flag_user[IDLEN+1];
};
