#!/bin/sh

report_file() {
    local bin="$1"
    echo "=== $1 ==="
    # 1. Print symbols sorted by size descending
    objdump -t "${bin}" | awk '
    $4 == ".data" || $4 == ".bss" || $4 == ".got.plt" {
        addr = strtonum("0x"$1)
        size = strtonum("0x"$5)
        page = and(addr, compl(0xfff))
        if (size > 0)
            printf "%5d B | Page 0x%06x | 0x%06x | %-8s | %s\n", size, page, addr, $4, $6
    }' | sort -nr

    echo "-------------------------------------------------------------------------"
    # 2. Calculate exact section & 4KB page breakdown
    {
        readelf -lW "${bin}" 2>/dev/null | awk '$1 == "GNU_RELRO" { print "RELRO_END", strtonum($3) + strtonum($6) }'
        objdump -h "${bin}" | awk '/^[ ]*[0-9]+[ ]+\.(data|bss|got\.plt)[ ]+/ { print "SEC", $2, strtonum("0x"$4), strtonum("0x"$3) }'
        objdump -t "${bin}" | awk '$4 == ".data" || $4 == ".bss" { print "SYM", $4, strtonum("0x"$5) }'
    } | awk '
    $1 == "RELRO_END" { relro_end = $2 }
    $1 == "SEC" {
        sec = $2; start = $3; size = $4; end = start + size
        sec_start[sec] = start; sec_size[sec] = size; sec_end[sec] = end
    }
    $1 == "SYM" { sym_sum[$2] += $3 }
    END {
        # Compute per-page breakdown for writable region (after GNU_RELRO)
        min_page = 0xffffffff; max_page = 0
        split(".got.plt .data .bss", secs, " ")
        for (i in secs) {
            s = secs[i]
            if (!(s in sec_start)) continue
            st = sec_start[s]; en = sec_end[s]
            if (relro_end > 0 && st < relro_end) st = relro_end
            if (st >= en) continue
            p_start = and(st, compl(0xfff))
            p_end   = and(en - 1, compl(0xfff))
            if (p_start < min_page) min_page = p_start
            if (p_end > max_page) max_page = p_end
            for (p = p_start; p <= p_end; p += 4096) {
                ov_s = (st > p) ? st : p
                ov_e = (en < p + 4096) ? en : (p + 4096)
                in_page[s, p] = ov_e - ov_s
                sec_pages[s]++
                page_total[p] += (ov_e - ov_s)
            }
        }

        printf "Section Breakdown (Writable Region after GNU_RELRO 0x%06x):\n", relro_end
        printf "  %-8s : %5d B (symbols: %5d B) | %5.2f pages | touches %d page(s):",
            ".data", sec_size[".data"], sym_sum[".data"], sec_size[".data"]/4096.0, sec_pages[".data"]
        for (p = min_page; p <= max_page; p += 4096)
            if (in_page[".data", p] > 0) printf " 0x%06x(%dB)", p, in_page[".data", p]
        printf "\n"

        printf "  %-8s : %5d B (symbols: %5d B) | %5.2f pages | touches %d page(s):",
            ".bss", sec_size[".bss"], sym_sum[".bss"], sec_size[".bss"]/4096.0, sec_pages[".bss"]
        for (p = min_page; p <= max_page; p += 4096)
            if (in_page[".bss", p] > 0) printf " 0x%06x(%dB)", p, in_page[".bss", p]
        printf "\n\n"

        total_pages = 0; total_bytes = 0
        printf "4KB Page Allocation Map (COW Pages per process):\n"
        for (p = min_page; p <= max_page; p += 4096) {
            total_pages++
            total_bytes += page_total[p]
            printf "  Page #%d [0x%06x ~ 0x%06x] : %4d / 4096 B used (%4d B free) |",
                total_pages, p, p + 4095, page_total[p], 4096 - page_total[p]
            if (in_page[".got.plt", p] > 0) printf " .got.plt=%dB", in_page[".got.plt", p]
            if (in_page[".data", p] > 0)    printf " .data=%dB", in_page[".data", p]
            if (in_page[".bss", p] > 0)     printf " .bss=%dB", in_page[".bss", p]
            printf "\n"
        }
        headroom = (max_page + 4096) - sec_end[".bss"]
        printf "-------------------------------------------------------------------------\n"
        printf "TOTAL WRITABLE STATIC : %d B (.data=%d B, .bss=%d B) -> %d physical 4KB Page(s) (%d B)\n",
            sec_size[".data"] + sec_size[".bss"], sec_size[".data"], sec_size[".bss"], total_pages, total_pages * 4096
        printf "Headroom before next 4KB page (0x%06x): %d B\n", max_page + 4096, headroom
    }'
    echo ""
}

main() {
    while [ "$#" -gt 0 ]; do
        report_file "$1"
        shift
    done
}

main "$@"
