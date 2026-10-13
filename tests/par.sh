# SPDX-License-Identifier: GPL-3.0-only
# Steps of a test script run side by side and reported in the order they were written (sourced by
# tests/run_tests.sh and tests/run_cr_tests.sh: POSIX sh, no make).
#
#   par_init DIR            the steps' logs and statuses go to DIR (emptied); PAR_JOBS steps at once
#                           (default: the number of cores; PAR_JOBS=1: one after another)
#   par_build CC ARGS...    a compile, through tests/hostcc.sh (kept when its command and inputs are unchanged)
#   par_prep CMD...         a file a test reads, made by CMD
#   par_run TITLE CMD...    a test
#   par_note KIND TEXT      a line for the report (a skipped section, a missing build), no command
#   par_wait ID             waits for the step; its status is the return value, its output DIR/ID.log
#   par_kill                ends what is still running (an early exit)
# Every call returns at once: the command starts in the background and the step's id is appended to $par_order
# (and left in $par_last). What a step waits for before it starts:
#   - a test: every compile and file-making step written above it since the test before it, and any step that makes
#     a file named on its command line (a program built further up, a package made for an earlier test);
#   - a file-making step: every step above it since the last test (they run in the order written);
#   - a compile: a step that makes an object named on its command line;
#   - PAR_AFTER="id id": steps that must have ended first, whatever their status (a test that reads what another
#     test writes). Set it on the line above the call; the call clears it.
# A step whose compile or file-making step failed does not run (status 125): the report stops at the failed step
# before it reaches it. The report is the caller's loop over $par_order: par_wait, then the log, so the output reads
# as when the steps ran one after another, and nothing is reported before everything above it has been.
# A step's stdout and stderr go to one file, as when the script's output is redirected to a file.

par_ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4; }

par_init() {
    PAR_DIR=$1
    par_n=${PAR_JOBS:-$(par_ncpu)}
    case $par_n in ''|*[!0-9]*|0) par_n=1 ;; esac
    mkdir -p "$PAR_DIR"
    rm -f "$PAR_DIR"/s*.log "$PAR_DIR"/s*.rc "$PAR_DIR"/s*.rc.tmp "$PAR_DIR"/sem.*
    mkfifo "$PAR_DIR/sem.$$"
    exec 9<>"$PAR_DIR/sem.$$"                    # the tokens: one byte each, PAR_JOBS of them
    rm -f "$PAR_DIR/sem.$$"
    par_i=0
    while [ $par_i -lt $par_n ]; do echo >&9; par_i=$((par_i + 1)); done
    par_step=0
    par_order=
    par_window=
    par_made=
    par_pids=
    par_last=
    PAR_AFTER=
}

# par_start ID "NEEDS" "AFTER" CMD...: CMD in the background once NEEDS have ended with status 0 and AFTER have ended
par_start() {
    par_id=$1; par_needs=$2; par_after=$3; shift 3
    (
        par_c=
        trap '[ -z "$par_c" ] || kill $par_c 2>/dev/null; exit 143' TERM
        par_rc=0
        for par_d in $par_needs $par_after; do
            while [ ! -f "$PAR_DIR/$par_d.rc" ]; do sleep 0.05; done
        done
        for par_d in $par_needs; do
            [ "$(cat "$PAR_DIR/$par_d.rc")" = 0 ] || par_rc=125
        done
        if [ $par_rc -eq 0 ]; then
            read -r par_t <&9
            "$@" > "$PAR_DIR/$par_id.log" 2>&1 &
            par_c=$!
            wait $par_c || par_rc=$?
            par_c=
            echo >&9
        else
            : > "$PAR_DIR/$par_id.log"
        fi
        echo $par_rc > "$PAR_DIR/$par_id.rc.tmp"
        mv "$PAR_DIR/$par_id.rc.tmp" "$PAR_DIR/$par_id.rc"
    ) &
    par_pids="$par_pids $!"
    eval "par_pid_$par_id=$!"
}

par_makers() {   # the steps that make a file named among the arguments
    for par_a in "$@"; do
        for par_m in $par_made; do
            [ "${par_m%%=*}" != "$par_a" ] || printf ' %s' "${par_m#*=}"
        done
    done
    return 0
}

par_new() {      # par_new KIND: the next step's id in $par_last
    par_step=$((par_step + 1))
    par_last=s$par_step
    par_order="$par_order $par_last"
    eval "par_kind_$par_last=\$1"
}

par_build() {
    par_out=; par_p=
    for par_a in "$@"; do [ "$par_p" = -o ] && par_out=$par_a; par_p=$par_a; done
    par_dep="$(par_makers "$@")"
    par_new build
    case "$par_out" in *" "*) ;; *) par_made="$par_made $par_out=$par_last" ;; esac
    par_window="$par_window $par_last"
    par_start $par_last "$par_dep" "$PAR_AFTER" sh tests/hostcc.sh "$@"
    PAR_AFTER=
}

par_prep() {     # (PAR_MAKES=file: a file the command makes that a later step names)
    par_dep="$par_window$(par_makers "$@")"
    par_new prep
    [ -z "${PAR_MAKES:-}" ] || par_made="$par_made $PAR_MAKES=$par_last"
    par_window="$par_window $par_last"
    par_start $par_last "$par_dep" "$PAR_AFTER" "$@"
    PAR_AFTER=
    PAR_MAKES=
}

par_run() {
    par_title=$1; shift
    par_dep="$par_window$(par_makers "$@")"
    par_new run
    eval "par_title_$par_last=\$par_title"
    par_window=
    par_start $par_last "$par_dep" "$PAR_AFTER" "$@"
    PAR_AFTER=
    PAR_MAKES=
}

par_note() {
    par_new "$1"
    eval "par_title_$par_last=\$2"
}

par_kind() { eval "printf '%s' \"\$par_kind_$1\""; }
par_title() { eval "printf '%s' \"\$par_title_$1\""; }

par_wait() {
    eval "par_w=\$par_pid_$1"
    wait "$par_w" 2>/dev/null || :
    [ -f "$PAR_DIR/$1.rc" ] || return 126
    return "$(cat "$PAR_DIR/$1.rc")"
}

par_kill() {
    [ -z "${par_pids:-}" ] || kill $par_pids 2>/dev/null || :
    par_pids=
}
