# Watchdog: POSIX Sinyal Tabanlı Süreç İzleme ve Yönetim Sistemi

Sakarya Üniversitesi, Yazılım Mühendisliği, Sistem Programlama 2026-2027 Dönem Projesi 1.

`watchdog`, kendi oluşturduğu child (worker) süreçleri izleyen ve yöneten bir C programıdır. Beklenmedik şekilde ölen worker'ı en fazla 3 kez yeniden başlatır, `SIGUSR1` ile durum raporu yazar, `SIGINT`/`SIGTERM` ile düzgün kapanır.

## Gereksinimler

- Linux (Ubuntu 20.04 üzerinde test edildi) veya WSL
- `gcc` ve `make`

## Derleme

```bash
make
```

Bu komut proje klasöründe iki çalıştırılabilir dosya üretir: `watchdog` ve `worker`.
`watchdog`, worker'ı `./worker` yoluyla başlattığı için **programı proje ana klasöründen çalıştırın**.

Temizlemek için: `make clean`

## Kullanım

```bash
./watchdog
```

Başka bir terminalden:

```bash
# Durum raporu iste (PID ve durumlar watchdog.log'a yazılır)
kill -SIGUSR1 $(pgrep -x watchdog)

# Düzgün kapat (Ctrl+C ile aynı sonuç)
kill -SIGTERM $(pgrep -x watchdog)

# Beklenmeyen ölümü test et (sadece worker PID'sini doğruladıktan sonra)
pgrep -x worker
kill -SIGKILL <WORKER_PID>
```

Tüm olaylar zaman damgasıyla `watchdog.log` dosyasına yazılır. Örnek çıktı: [`docs/sample_watchdog.log`](docs/sample_watchdog.log)

## Worker modları

Worker davranışı `WORKER_MODE` ortam değişkeniyle seçilir (test amaçlı):

| Mod | Davranış |
|---|---|
| `run` (varsayılan) | SIGTERM gelene kadar çalışır |
| `exit` | 1 saniye sonra kod 0 ile çıkar (normal sonlanma) |
| `fail` | 1 saniye sonra kod 7 ile çıkar (hatalı sonlanma) |
| `ignore-term` | SIGTERM'i yok sayar (shutdown timeout testi) |

Örnek: `WORKER_MODE=fail ./watchdog`

## Tasarım özeti

| Parametre | Değer |
|---|---|
| Worker sayısı | 2 |
| Maksimum restart (worker başına) | 3 |
| Periyodik kontrol (SIGALRM, `setitimer`) | 1 saniye |
| Shutdown timeout (sonra SIGKILL) | 5 saniye |
| Log dosyası | `watchdog.log` |

- Sinyal handler'ları sadece bayrak (`volatile sig_atomic_t`) set eder. Asıl işlem ana döngüde yapılır.
- Normal çıkış (kod 0) restart edilmez. Hata koduyla çıkış veya sinyalle ölüm restart edilir.
- Shutdown sırasında yeni worker başlatılmaz. Tüm worker'lara SIGTERM gönderilir, timeout dolarsa SIGKILL gönderilir.
- Logger `write()` ile tek çağrıda satır yazar (`O_APPEND`), `EINTR` durumunu yönetir.

## Testler

```bash
make test
```

veya

```bash
bash tests/test_scenarios.sh
```

Script derlemeyi yapar ve ödev Bölüm 7'deki senaryoları otomatik doğrular:

1. Normal çalışma (2 worker başlar)
2. Normal sonlanma (restart yok)
3. Beklenmeyen sonlanma (SIGKILL sonrası restart)
4. Restart limiti (1 ilk başlatma + 3 restart, sonra durur)
5. Durum raporu (SIGUSR1)
6. Graceful shutdown (SIGTERM) ve log doğrulama
7. Graceful shutdown (SIGINT / Ctrl+C)
8. Shutdown zaman aşımı (SIGTERM'i yok sayan worker)

Beklenen sonuç: `SONUC: 25 gecti, 0 kaldi`

## Dosya yapısı

```
Makefile
README.md
include/   signal_manager.h  process_manager.h  logger.h
src/       main.c  signal_manager.c  process_manager.c  logger.c
workers/   worker.c
tests/     test_scenarios.sh  (+ signal_manager_check.c, main_test_stubs.c, main_timeout_stubs.c)
docs/      sample_watchdog.log
```

## Görev dağılımı

| Öğrenci | Modül |
|---|---|
| Sedef Aydos | Student 1: Sinyal yönetimi ve ana program (`signal_manager`, `main.c`) |
| Ceren Gülesen | Student 2: Child süreç yönetimi (`process_manager`, `worker.c`) |
| Kürşat Keskin | Student 3: Logger, Makefile, test scripti, README ve test dokümantasyonu (`logger`, `Makefile`, `tests/test_scenarios.sh`) |