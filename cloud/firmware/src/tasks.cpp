#include "tasks.h"
#include <Preferences.h>

static_assert(sizeof(Task) == 49, "Task layout changed - NVS migration needed");

// previous on-flash layout (v1, pre v3.20): no id field
struct __attribute__((packed)) TaskV1 {
  uint8_t  done, hasEnd, hour, minute;
  uint16_t year;
  uint8_t  month, day;
  uint16_t endYear;
  uint8_t  endMonth, endDay;
  char     title[24];
};
static_assert(sizeof(TaskV1) == 36, "TaskV1 layout");

#define TASK_MAGIC 0x3C
#define TASK_LOOKAHEAD 14          // widget: how far ahead to search for the next task day

static Task list[MAX_TASKS];
static uint8_t nTasks = 0;

void tasksSave() {
  Preferences p;
  p.begin("buddy", false);
  uint8_t buf[3 + MAX_TASKS * sizeof(Task)];
  buf[0] = TASK_MAGIC;
  buf[1] = 2;                  // layout version
  buf[2] = nTasks;
  memcpy(buf + 3, list, nTasks * sizeof(Task));
  p.putBytes("tasks", buf, 3 + nTasks * sizeof(Task));
  p.end();
}

// "L" + 11 hex chars: a local id that can never collide with the ids the
// Vercel server hands out ("V" + 11 hex)
void taskGenId(char out[TASK_ID_LEN]) {
  snprintf(out, TASK_ID_LEN, "L%011llx",
           (unsigned long long)((uint64_t)random(0x10000) << 32 | (uint32_t)random(0x10000) << 16 | (uint32_t)random(0x10000) & 0xFFFFULL));
}

// an id is 2..12 chars: 'L' or 'V' + lowercase hex (device / server origin)
static bool idOk(const char* s) {
  if (!s[0]) return false;
  if (s[0] != 'L' && s[0] != 'V') return false;
  int n = 0;
  for (int i = 1; s[i] && i < TASK_ID_LEN; i++, n++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return n >= 1 && n <= 11;
}

void tasksBegin() {
  memset(list, 0, sizeof(list));
  nTasks = 0;
  Preferences p;
  p.begin("buddy", true);
  size_t len = p.getBytesLength("tasks");
  if (len >= 3 && len <= 3 + MAX_TASKS * sizeof(Task)) {
    static uint8_t buf[3 + MAX_TASKS * sizeof(Task)];
    p.getBytes("tasks", buf, len);
    if (buf[0] == TASK_MAGIC && buf[1] == 2 && buf[2] <= MAX_TASKS &&
        len >= 3 + (size_t)buf[2] * sizeof(Task)) {
      nTasks = buf[2];
      memcpy(list, buf + 3, nTasks * sizeof(Task));
    } else if (buf[0] == TASK_MAGIC && buf[1] == 1 && buf[2] <= MAX_TASKS &&
               len >= 3 + (size_t)buf[2] * sizeof(TaskV1)) {
      // migrate v1 -> v2 (keep the task, give it a fresh local id)
      nTasks = buf[2];
      for (int i = 0; i < nTasks; i++) {
        TaskV1 o;
        memcpy(&o, buf + 3 + i * sizeof(TaskV1), sizeof(o));
        Task& n = list[i];
        n.done = o.done; n.hasEnd = o.hasEnd; n.hour = o.hour; n.minute = o.minute;
        n.year = o.year; n.month = o.month; n.day = o.day;
        n.endYear = o.endYear; n.endMonth = o.endMonth; n.endDay = o.endDay;
        memcpy(n.title, o.title, sizeof(o.title));
        n.title[sizeof(o.title)] = 0;
        taskGenId(n.id);
      }
      Serial.println("[TASK] migrated saved tasks to the id format");
    }
  }
  p.end();
  bool dirty = false;
  for (int i = 0; i < nTasks; i++)              // any pre-id task gets one now
    if (!idOk(list[i].id)) { taskGenId(list[i].id); dirty = true; }
  if (dirty) tasksSave();
  Serial.printf("[TASK] loaded %d tasks\n", (int)nTasks);
}

int tasksCount() { return nTasks; }
int tasksCapacity() { return MAX_TASKS; }

Task taskGet(int i) {
  Task t; memset(&t, 0, sizeof(t));
  if (i >= 0 && i < nTasks) t = list[i];
  return t;
}

int taskFind(const char* id) {
  if (!id || !id[0]) return -1;
  for (int i = 0; i < nTasks; i++)
    if (strncmp(list[i].id, id, TASK_ID_LEN) == 0) return i;
  return -1;
}

int taskAdd(const Task& t) {
  if (nTasks >= MAX_TASKS) return -1;
  list[nTasks] = t;
  if (!idOk(list[nTasks].id)) taskGenId(list[nTasks].id);
  nTasks++;
  tasksSave();
  return nTasks - 1;
}

bool taskUpdate(int i, const Task& t) {
  if (i < 0 || i >= nTasks) return false;
  list[i] = t;
  if (!idOk(list[i].id)) taskGenId(list[i].id);
  tasksSave();
  return true;
}

bool taskRemove(int i) {
  if (i < 0 || i >= nTasks) return false;
  for (int j = i; j < nTasks - 1; j++) list[j] = list[j + 1];
  nTasks--;
  tasksSave();
  return true;
}

void tasksReplaceAll(const Task* entries, int n, bool save) {
  if (n < 0) n = 0;
  if (n > MAX_TASKS) n = MAX_TASKS;
  if (entries && n) memcpy(list, entries, n * sizeof(Task));
  nTasks = (uint8_t)n;
  if (save) tasksSave();
}

// ---------------- day helpers ----------------

// days since 1970-01-01 (civil-from-days algorithm) - safe across month and
// year edges, unlike a plain y*10000+m*100+d key
static uint32_t dayNum(uint16_t y, uint8_t m, uint8_t d) {
  y = (uint16_t)(y - (m <= 2 ? 1 : 0));
  uint32_t era = y / 400;
  uint32_t yoe = y - era * 400;                      // [0, 399]
  uint32_t mp = (m + 9) % 12;                        // Mar = 0
  uint32_t doy = (153 * mp + 2) / 5 + d - 1;         // [0, 365]
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static uint32_t dayNumTm(const struct tm& t) {
  return dayNum((uint16_t)(t.tm_year + 1900), (uint8_t)(t.tm_mon + 1), (uint8_t)t.tm_mday);
}

bool taskOnDay(const Task& t, const struct tm& day) {
  uint32_t k = dayNumTm(day);
  uint32_t s = dayNum(t.year, t.month, t.day);
  uint32_t e = t.hasEnd ? dayNum(t.endYear, t.endMonth, t.endDay) : s;
  return k >= s && k <= e;
}

bool taskStartsOn(const Task& t, const struct tm& day) {
  return dayNumTm(day) == dayNum(t.year, t.month, t.day);
}

int tasksForDay(const struct tm& day, int* idxOut, int cap) {
  struct Ent { uint32_t score; int idx; };
  Ent ents[MAX_TASKS];
  int n = 0;
  for (int i = 0; i < nTasks; i++) {
    if (!taskOnDay(list[i], day)) continue;
    uint32_t tm = (list[i].hour == TASK_NO_TIME) ? 24000u
                                                 : (uint32_t)list[i].hour * 60u + list[i].minute;
    ents[n].score = ((uint32_t)(list[i].done ? 1 : 0) << 31) | (tm << 8) | (uint32_t)i;
    ents[n].idx = i;
    n++;
  }
  for (int a = 1; a < n; a++) {                      // insertion sort by score
    Ent k = ents[a]; int b = a - 1;
    while (b >= 0 && ents[b].score > k.score) { ents[b + 1] = ents[b]; b--; }
    ents[b + 1] = k;
  }
  int fill = (n < cap) ? n : cap;
  for (int i = 0; i < fill; i++) idxOut[i] = ents[i].idx;
  return n;
}

void tasksCountDay(const struct tm& day, int& total, int& done) {
  total = done = 0;
  for (int i = 0; i < nTasks; i++)
    if (taskOnDay(list[i], day)) { total++; if (list[i].done) done++; }
}

bool taskPickDay(struct tm& dayOut, int& total, int& done) {
  time_t now = time(nullptr);
  struct tm base; localtime_r(&now, &base);
  if (base.tm_year + 1900 < 2025) return false;      // clock never synced
  base.tm_hour = 12; base.tm_min = 0; base.tm_sec = 0;   // noon keeps +N days clear of DST edges
  time_t start = mktime(&base);
  for (int off = 0; off <= TASK_LOOKAHEAD; off++) {
    time_t d = start + (time_t)off * 86400;
    struct tm day; localtime_r(&d, &day);
    tasksCountDay(day, total, done);
    if (total > 0) { dayOut = day; return true; }
  }
  return false;
}

void taskDayLabel(const struct tm& day, char* out, size_t cap) {
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  long diff = (long)(dayNumTm(day) - dayNumTm(t));
  if (diff == 0) snprintf(out, cap, "TODAY");
  else if (diff == 1) snprintf(out, cap, "TMRW");
  else {
    char wd[8];
    struct tm d = day;
    strftime(wd, sizeof(wd), "%a", &d);
    snprintf(out, cap, "%s %d", wd, day.tm_mday);
  }
}
