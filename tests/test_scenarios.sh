#!/bin/bash
# Watchdog test senaryolari (odev Bolum 7)
# Calistirma: bash tests/test_scenarios.sh   veya   make test

cd "$(dirname "$0")/.." || exit 1
make -s || { echo "make basarisiz"; exit 1; }

LOG=watchdog.log
PASS=0
FAIL=0
WD_PID=0
RC=0

check() {
    local desc="$1"; shift
    if "$@"; then
        echo "  [PASS] $desc"; PASS=$((PASS + 1))
    else
        echo "  [FAIL] $desc"; FAIL=$((FAIL + 1))
    fi
}

start_wd() {            # arguman: WORKER_MODE (run, exit, fail, ignore-term)
    rm -f "$LOG"
    WORKER_MODE="$1" ./watchdog >/dev/null 2>&1 &
    WD_PID=$!
    sleep 1
}

stop_and_wait() {       # arguman: sinyal adi (TERM veya INT)
    kill -"$1" "$WD_PID" 2>/dev/null
    wait "$WD_PID" 2>/dev/null
    RC=$?
}

worker_pids()   { pgrep -P "$WD_PID" -x worker; }
log_has()       { grep -q -- "$1" "$LOG"; }
log_count()     { grep -c -- "$1" "$LOG" || true; }
no_workers()    { ! pgrep -x worker >/dev/null; }
bad_log_lines() { grep -vcE '^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] ' "$LOG" || true; }

echo "== 1. Normal calisma =="
start_wd run
check "iki worker calisiyor" test "$(worker_pids | wc -l)" -eq 2
check "logda 2 'started' satiri var" test "$(log_count 'started PID=')" -eq 2
stop_and_wait TERM

echo "== 2. Normal sonlanma (worker kod 0 ile cikar) =="
start_wd exit
sleep 4
check "normal cikis loglandi" test "$(log_count 'ended normally')" -eq 2
check "yeniden baslatma yapilmadi" test "$(log_count 'started PID=')" -eq 2
stop_and_wait TERM

echo "== 3. Beklenmeyen sonlanma (SIGKILL) =="
start_wd run
VICTIM=$(worker_pids | head -1)
echo "  kurban worker PID=$VICTIM"
kill -KILL "$VICTIM"
sleep 3
check "sinyalle olum loglandi" log_has "terminated by signal=9"
check "worker yeniden baslatildi (toplam 3 start)" test "$(log_count 'started PID=')" -eq 3
stop_and_wait TERM

echo "== 4. Restart limiti (worker surekli hata ile cikar) =="
start_wd fail
sleep 9
check "Worker 1 tam 4 kez basladi (1 ilk + 3 restart)" test "$(log_count 'Worker 1 started')" -eq 4
check "hata kodu loglandi" log_has "exited code=7"
sleep 3
check "limit asildiktan sonra yeni start yok" test "$(log_count 'Worker 1 started')" -eq 4
stop_and_wait TERM

echo "== 5. Durum raporu (SIGUSR1) =="
start_wd run
kill -USR1 "$WD_PID"
sleep 1
check "rapor basligi var" log_has "STATUS REPORT begin"
check "iki worker satiri var" test "$(log_count 'STATUS worker')" -eq 2
check "rapor sonu var" log_has "STATUS REPORT end"
stop_and_wait TERM

echo "== 6. Graceful shutdown (SIGTERM) + log dogrulama =="
start_wd run
stop_and_wait TERM
check "watchdog kod 0 ile cikti" test "$RC" -eq 0
check "worker kalmadi" no_workers
check "'Watchdog started' var" log_has "Watchdog started"
check "'Shutdown initiated' var" log_has "Shutdown initiated"
check "'All child processes stopped' var" log_has "All child processes stopped"
check "'Watchdog stopped' var" log_has "Watchdog stopped"
check "tum satirlarda zaman damgasi var" test "$(bad_log_lines)" -eq 0

echo "== 7. Graceful shutdown (SIGINT / Ctrl+C) =="
start_wd run
stop_and_wait INT
check "watchdog kod 0 ile cikti" test "$RC" -eq 0
check "worker kalmadi" no_workers
check "'Watchdog stopped' var" log_has "Watchdog stopped"

echo "== 8. Shutdown zaman asimi (SIGTERM'i yok sayan worker) =="
start_wd ignore-term
stop_and_wait TERM
check "zaman asimi loglandi (SIGKILL)" log_has "Shutdown timeout expired"
check "worker kalmadi" no_workers
check "watchdog temiz kapandi" log_has "Watchdog stopped"

echo
echo "SONUC: $PASS gecti, $FAIL kaldi"
[ "$FAIL" -eq 0 ]