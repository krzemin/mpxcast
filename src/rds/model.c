#include "rds/model.h"

#include <string.h>

void rds_station_info_init(struct rds_station_info *info) { memset(info, 0, sizeof(*info)); }

void rds_station_event_init(struct rds_station_event *event) { memset(event, 0, sizeof(*event)); }
