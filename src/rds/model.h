#ifndef MPXCAST_RDS_MODEL_H
#define MPXCAST_RDS_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rds_station_info {
    bool has_pi;
    uint16_t pi;
    bool has_group;
    char group[3];
    bool tp;
    bool ta;
    uint8_t pty;
    char ps[9];
    char radiotext[65];
};

struct rds_station_event {
    bool station_changed;
    bool ps_changed;
    bool radiotext_changed;
    bool ps_preview_changed;
    bool radiotext_preview_changed;
    uint8_t corrected_block_count;
    char ps_preview[9];
    char radiotext_preview[65];
    struct rds_station_info info;
};

void rds_station_info_init(struct rds_station_info *info);
void rds_station_event_init(struct rds_station_event *event);

#endif
