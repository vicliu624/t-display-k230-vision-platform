# Actual RISC-V disassembly: the only raw gnne_enable call must be the one
# inside our adapter. All vendor initialization and submission call sites
# must reach adapters, including relaxed jalr calls in the large model ELF.
/^[[:xdigit:]]+ <.*>:$/ { caller = $0 }
/[[:space:]](jal|jalr|j|jr)[[:space:]]/ && /<gnne_init>/ { bypass = 1 }
/[[:space:]](jal|jalr|j|jr)[[:space:]]/ && /<gnne_enable>/ {
    if (caller !~ / <__wrap_gnne_enable>:$/) bypass = 1
    raw_start++
}
/[[:space:]](jal|jalr|j|jr)[[:space:]]/ && /<__wrap_gnne_init>/ { guarded_init++; print }
/[[:space:]](jal|jalr|j|jr)[[:space:]]/ && /<__wrap_gnne_enable>/ { guarded_start++; print }
END {
    if (bypass || !guarded_init || !guarded_start || raw_start != 1) {
        print "FAIL nncase KPU call sites bypass/miss production adapters" > "/dev/stderr"
        exit 1
    }
}
