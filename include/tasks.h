#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"

// ============================================================
//  tasks.h - persistent to-do list bound to calendar days (NVS)
//
//  A TASK is a checklist item, not an alarm: it never rings.
//  It starts on a date, can run until an end date (spanning
//  several days, like "conference trip 3rd-9th") and can carry
//  an optional time of day that is only used for sorting and
//  display. Tasks live on the Tasks widget / menu screen and in
//  the calendar of the web app; alarms + reminders keep using
//  alarms.h.
// ============================================================

#define TASK_TITLE_LEN 24      // 23 visible characters

enum : uint8_t { TASK_NO_TIME = 255 };    // task.hour when no time is set

struct __attribute__((packed)) Task {
  uint8_t  done;      // 1 = checked off (kept until deleted)
  uint8_t  hasEnd;    // 1 = runs until endYear/endMonth/endDay
  uint8_t  hour;      // 0..23 or TASK_NO_TIME
  uint8_t  minute;    // 0..59
  uint16_t year;      // first day
  uint8_t  month;     // 1..12
  uint8_t  day;       // 1..31
  uint16_t endYear;   // last day (hasEnd only)
  uint8_t  endMonth;
  uint8_t  endDay;
  char     title[TASK_TITLE_LEN];   // zero-terminated, sanitized
};

void  tasksBegin();                    // load from NVS
int   tasksCount();
int   tasksCapacity();
Task  taskGet(int i);                  // zeroed Task on bad idx
int   taskAdd(const Task& t);          // idx or -1 (full)
bool  taskUpdate(int i, const Task& t);
bool  taskRemove(int i);
void  tasksSave();                     // persist now (auto after mutations)

// ---- day helpers ----

// is this task active on the given calendar day?
bool  taskOnDay(const Task& t, const struct tm& day);

// does the day fall on the task's first day?
bool  taskStartsOn(const Task& t, const struct tm& day);

// indices of the tasks active on `day`, best first: undone before done,
// timed before untimed, earlier time first (insertion order within a group).
// returns the total active that day; fills at most `cap` into idxOut.
int   tasksForDay(const struct tm& day, int* idxOut, int cap);

// total + done counts for one day
void  tasksCountDay(const struct tm& day, int& total, int& done);

// the day the widget shows: today if anything is due, else the next day
// with tasks (up to 14 days ahead). false = nothing planned at all.
bool  taskPickDay(struct tm& dayOut, int& total, int& done);

// short label for the widget header: "TODAY" / "TMRW" / "Fri 6"
void  taskDayLabel(const struct tm& day, char* out, size_t cap);
