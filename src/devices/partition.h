#ifndef DEVICES_PARTITION_H
#define DEVICES_PARTITION_H

struct block_device;

void partition_scan (struct block_device *);

#endif /* devices/partition.h */
