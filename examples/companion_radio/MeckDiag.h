#pragma once
// =============================================================================
// Freeze and reset diagnostics (branch diag/freeze, temporary)
//
// Nothing used to record why the deck froze or restarted. This keeps, across a
// restart (RTC_NOINIT memory survives panic, watchdog and software resets):
//   - breadcrumbs: the last step taken around sleep, lock, keys and HA updates
//   - a heartbeat every 5 s: uptime, free internal RAM, largest block, PSRAM
//   - slow loop() passes (more than 2.5 s, time asleep not counted)
// and puts the loop task on the task watchdog (90 s, panic): a hang then
// panics into the core dump partition instead of staying frozen.
//
// At the next boot, once the SD card is up, one line per boot goes to
// /meshcore/diag.log and to serial: the reset reason, what the previous run
// last did, and the core dump summary (task, PC, backtrace; decode the
// addresses with addr2line against the matching firmware.elf).
//
// Reading the lines: reason POWERON with a crumb = reset button or power loss
// (the deck was stuck or was reset by hand); TASK_WDT / PANIC / INT_WDT = it
// crashed or hung and the dump says where; crumb 10 as the last = it never
// came back from light sleep, 11 = it woke and then stopped.
// =============================================================================

#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_core_dump.h>
#include <esp_attr.h>
#include <SD.h>

enum MeckCrumb : uint8_t {
  MC_NONE = 0,
  MC_SLEEP_ENTER = 10,    // about to light-sleep
  MC_SLEEP_EXIT = 11,     // woke; the low byte of the value is the wake cause
  MC_LOCK_BEGIN = 20, MC_LOCK_END = 21,       // lock policy (radios paused)
  MC_UNLOCK_BEGIN = 22, MC_UNLOCK_END = 23,   // lock policy (radios back)
  MC_KEY = 30,            // a key was handled; value = the key
  MC_HA_BEGIN = 40, MC_HA_END = 41,           // Home Assistant update task
  MC_BOOT_DONE = 50,      // setup() finished
};

struct MeckDiagSlow { uint32_t atMs; uint32_t ms; uint32_t crumb; uint32_t crumbAgoMs; };

struct MeckDiagRtc {
  uint32_t magic;
  uint32_t boots;            // counted since the last power-on
  uint32_t upMs;             // uptime at the last heartbeat
  uint32_t crumb;            // MeckCrumb in the top byte, value in the low 24 bits
  uint32_t crumbMs;
  uint32_t freeInt, largest, freePsram, minFreeInt;
  uint8_t  nSlow;
  MeckDiagSlow slow[6];
};

#define MECK_DIAG_MAGIC 0x4D44494Du

RTC_NOINIT_ATTR static MeckDiagRtc meckDiagRtc;
static MeckDiagRtc meckDiagPrev;       // what the previous run left
static bool meckDiagPrevValid = false;
static esp_reset_reason_t meckDiagReason = ESP_RST_UNKNOWN;

static const char* meckDiagReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    default:                return "UNKNOWN";
  }
}

// First thing in setup()
static void meckDiagBoot() {
  meckDiagReason = esp_reset_reason();
  if (meckDiagRtc.magic == MECK_DIAG_MAGIC && meckDiagReason != ESP_RST_POWERON) {
    meckDiagPrev = meckDiagRtc;
    meckDiagPrevValid = true;
    uint32_t boots = meckDiagRtc.boots + 1;
    memset(&meckDiagRtc, 0, sizeof(meckDiagRtc));
    meckDiagRtc.boots = boots;
  } else {
    memset(&meckDiagRtc, 0, sizeof(meckDiagRtc));
  }
  meckDiagRtc.magic = MECK_DIAG_MAGIC;
  meckDiagRtc.minFreeInt = 0xFFFFFFFF;
}

static inline void meckCrumb(MeckCrumb id, uint32_t value = 0) {
  meckDiagRtc.crumb = ((uint32_t)id << 24) | (value & 0xFFFFFF);
  meckDiagRtc.crumbMs = millis();
}

// End of setup(): the loop task goes on the task watchdog
static void meckDiagWatchdogStart() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 90000;
  cfg.idle_core_mask = (1 << 0);     // as before: IDLE0 only
  cfg.trigger_panic = true;          // panic = core dump to flash
  esp_task_wdt_reconfigure(&cfg);
  esp_task_wdt_add(NULL);            // the loop task
}

// Top of loop(); sleptUs = total time spent in light sleep so far
static void meckDiagTick(uint64_t sleptUs) {
  static uint32_t lastUs = 0, lastHeartMs = 0;
  static uint64_t lastSlept = 0;
  uint32_t nowUs = micros();
  uint32_t nowMs = millis();
  esp_task_wdt_reset();
  if (lastUs) {
    uint64_t sleptDelta = sleptUs - lastSlept;
    uint32_t passUs = nowUs - lastUs;
    uint32_t awakeMs = passUs / 1000 > sleptDelta / 1000 ? (uint32_t)(passUs / 1000 - sleptDelta / 1000) : 0;
    if (awakeMs > 2500) {
      MeckDiagRtc& d = meckDiagRtc;
      int i;
      if (d.nSlow < 6) i = d.nSlow++;
      else { for (int k = 0; k < 5; k++) d.slow[k] = d.slow[k + 1]; i = 5; }
      d.slow[i] = { nowMs, awakeMs, d.crumb, nowMs - d.crumbMs };
      Serial.printf("[Diag] slow loop pass: %lu ms (last crumb %u:%lu, %lu ms ago)\n",
                    (unsigned long)awakeMs, (unsigned)(d.crumb >> 24), (unsigned long)(d.crumb & 0xFFFFFF),
                    (unsigned long)(nowMs - d.crumbMs));
    }
  }
  lastUs = nowUs;
  lastSlept = sleptUs;
  if (nowMs - lastHeartMs >= 5000) {
    lastHeartMs = nowMs;
    MeckDiagRtc& d = meckDiagRtc;
    d.upMs = nowMs;
    d.freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    d.largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    d.freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t mn = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (mn < d.minFreeInt) d.minFreeInt = mn;
  }
}

// SD card up: say why this boot happened. Reads and erases the core dump.
static void meckDiagReport() {
  char line[700];
  int n = snprintf(line, sizeof(line), "boot#%lu reason=%s",
                   (unsigned long)meckDiagRtc.boots, meckDiagReasonName(meckDiagReason));
  if (meckDiagPrevValid) {
    const MeckDiagRtc& p = meckDiagPrev;
    n += snprintf(line + n, sizeof(line) - n,
                  " prev_up=%lus crumb=%u:%lu@%lus free_int=%lu largest=%lu min_free_int=%lu psram=%lu",
                  (unsigned long)(p.upMs / 1000), (unsigned)(p.crumb >> 24), (unsigned long)(p.crumb & 0xFFFFFF),
                  (unsigned long)(p.crumbMs / 1000), (unsigned long)p.freeInt, (unsigned long)p.largest,
                  (unsigned long)p.minFreeInt, (unsigned long)p.freePsram);
    for (int i = 0; i < p.nSlow && i < 6; i++)
      n += snprintf(line + n, sizeof(line) - n, " slow[%lus %lums crumb %u:%lu -%lums]",
                    (unsigned long)(p.slow[i].atMs / 1000), (unsigned long)p.slow[i].ms,
                    (unsigned)(p.slow[i].crumb >> 24), (unsigned long)(p.slow[i].crumb & 0xFFFFFF),
                    (unsigned long)p.slow[i].crumbAgoMs);
  }
  if (esp_core_dump_image_check() == ESP_OK) {
    esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)heap_caps_malloc(sizeof(*s), MALLOC_CAP_SPIRAM);
    if (s && esp_core_dump_get_summary(s) == ESP_OK) {
      n += snprintf(line + n, sizeof(line) - n, " coredump task=%s pc=0x%08lx bt=", s->exc_task, (unsigned long)s->exc_pc);
      for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16 && n < (int)sizeof(line) - 14; i++)
        n += snprintf(line + n, sizeof(line) - n, "%08lx ", (unsigned long)s->exc_bt_info.bt[i]);
    } else {
      n += snprintf(line + n, sizeof(line) - n, " coredump=present(unreadable)");
    }
    if (s) heap_caps_free(s);
    esp_core_dump_image_erase();
  }
  Serial.printf("[Diag] %s\n", line);
  if (SD.exists("/meshcore") || SD.mkdir("/meshcore")) {
    File f = SD.open("/meshcore/diag.log", FILE_APPEND);
    if (f) { f.println(line); f.close(); }
  }
#ifdef SDCARD_CS
  digitalWrite(SDCARD_CS, HIGH);
#endif
}
