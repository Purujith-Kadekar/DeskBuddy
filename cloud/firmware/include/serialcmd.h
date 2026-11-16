// ============================================================
//  serialcmd.h - the USB CDC command channel (v3.21)
//
//  Until v3.21 the USB port was TELEMETRY ONLY: the firmware printed
//  its banner and its logs there, and nothing it received was read.
//  That is why the Ventuno Q could not drive the buddy over the
//  cable it is already plugged into.
//
//  v3.21 adds a line protocol on the same port.  Every command mirrors
//  something the REST API already does, so nothing new is invented and
//  no widget, feature or setting is removed: the buddy keeps its web
//  UI, its planner, its widgets, its touch gestures, its clock, its
//  weather and its OTA door exactly as they were.
//
//  The line protocol (one command per line, LF terminated, answers come
//  back on the same port as "[CMD] ok ..." / "[CMD] err ..."):
//
//    FACE <token>            set the expression (one of the 41 names)
//    ACT <action>            pet|party|sleep|wake|widgets|clock|next|
//                            weather|face|reboot
//    NOTIFY <label>|<melody> show a reminder notification now
//    DISMISS                 stop the ringing
//    SNOOZE                  snooze the ringing
//    MELODY <n>              play a tone (0-5 alarm tones, 6-8 reminder)
//    ALARM <json>            add/update an alarm (the REST body)
//    TASK <json>             add/update a task (the REST body)
//    DEL <alarm|task> <id>   delete an entry
//    POMO <action>           start|pause|reset|skip|lens
//    SET <json>              update the settings (the REST body)
//    STATUS                  print a one-line JSON status
//    PING                    liveness
//
//  The Ventuno Q uses this over /dev/ventuno-face (the udev symlink).
//  A host that does not know this protocol can keep using the REST API
//  on port 80 - the two are independent.
// ============================================================
#pragma once

#include <Arduino.h>

void serialCmdBegin();   // prepare the line buffer (call once, in setup)
void serialCmdTick();    // read and answer commands (call from the loop)
