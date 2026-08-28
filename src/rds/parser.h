#ifndef MPXCAST_RDS_PARSER_H
#define MPXCAST_RDS_PARSER_H

#include "rds/block_sync.h"
#include "rds/model.h"

struct rds_parser_state {
    struct rds_station_info info;
    bool radiotext_ab;
    bool radiotext_ab_valid;
    uint8_t ps_seen_mask;
    uint64_t radiotext_seen_mask;
    float ps_segment_quality[4];
    float radiotext_quality[64];
    char last_reported_ps[9];
    char last_reported_radiotext[65];
    char last_reported_ps_preview[9];
    char last_reported_radiotext_preview[65];
};

void rds_parser_init(struct rds_parser_state *state);
bool rds_parser_process_group(struct rds_parser_state *state, const struct rds_group *group,
                              struct rds_station_event *event);
const struct rds_station_info *rds_parser_get_station_info(const struct rds_parser_state *state);
bool rds_parser_has_complete_radiotext(const struct rds_parser_state *state);

#endif
