/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Util side of <onion/report.h> and <onion/boot_safety.h>.
 *
 *   report_collect_previous_session()  turns crash traces the previous
 *       session left (util and daemon crash logs, a ShellUI that died inside
 *       the Home script injection) into local reports. Call it before util
 *       clears its own crash log.
 *   report_sender_start()  marks this start "running" once the stack has been
 *       up for ONION_BOOT_RUNNING_AFTER_SECONDS, then sends pending reports
 *       when the build has a report URL (-DANIKETOSHEN_REPORT_URL=...) and
 *       config.ini [reports] send is true. A few per session, scrubbed again
 *       (now with the local user names), deleted once the relay accepted them.
 */
#pragma once

void report_collect_previous_session();
void report_sender_start();
