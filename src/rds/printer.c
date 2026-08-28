#include "rds/printer.h"

#include "core/logging.h"

void rds_printer_print_event(const struct rds_station_event *event) {
    const char *ps_prefix = "";
    const char *ps = "";
    const char *ps_suffix = "";
    const char *radiotext_prefix = "";
    const char *radiotext = "";
    const char *radiotext_suffix = "";
    const bool has_text_update = event->ps_changed || event->ps_preview_changed ||
                                 event->radiotext_changed || event->radiotext_preview_changed;
    const bool has_signal_issue = event->corrected_block_count > 0u;

    if (logging_get_level() != LOG_TRACE || !event->station_changed) {
        return;
    }
    if (!has_text_update && !has_signal_issue) {
        return;
    }

    if (event->ps_changed) {
        ps_prefix = " ps=\"";
        ps = event->info.ps;
        ps_suffix = "\"";
    } else if (event->ps_preview_changed) {
        ps_prefix = " ps-partial=\"";
        ps = event->ps_preview;
        ps_suffix = "\"";
    }
    if (event->radiotext_changed) {
        radiotext_prefix = " radiotext=\"";
        radiotext = event->info.radiotext;
        radiotext_suffix = "\"";
    } else if (event->radiotext_preview_changed) {
        radiotext_prefix = " radiotext-partial=\"";
        radiotext = event->radiotext_preview;
        radiotext_suffix = "\"";
    }

    TRACE("RDS update: group=%s pi=0x%04X tp=%u ta=%u pty=%u corrected=%u%s%s%s%s%s%s",
          event->info.has_group ? event->info.group : "--",
          event->info.has_pi ? event->info.pi : 0u, event->info.tp ? 1u : 0u,
          event->info.ta ? 1u : 0u, (unsigned)event->info.pty,
          (unsigned)event->corrected_block_count, ps_prefix, ps, ps_suffix, radiotext_prefix,
          radiotext, radiotext_suffix);
}
