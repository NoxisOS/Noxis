/**
 * @file    drivers/ata.h
 * @brief   ATA PIO disk driver
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef DRIVERS_ATA_H
#define DRIVERS_ATA_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define ATA_PRIMARY      0
#define ATA_SECONDARY    1
#define ATA_MASTER       0
#define ATA_SLAVE        1
#define ATA_SECTOR_SIZE  512

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes and detects the ATA drive
 * @param bus    ATA_PRIMARY or ATA_SECONDARY
 * @param drive  ATA_MASTER or ATA_SLAVE
 * @return OS_OK if drive detected, OS_ERR_NOT_FOUND otherwise
 */
os_status_t ata_init(uint8_t bus, uint8_t drive);

/**
 * @brief Reads sectors from the drive using PIO
 * @param bus    ATA_PRIMARY or ATA_SECONDARY
 * @param drive  ATA_MASTER or ATA_SLAVE
 * @param lba    Starting logical block address
 * @param count  Number of sectors to read
 * @param buf    Output buffer (must be count * 512 bytes)
 * @return OS_OK on success
 */
os_status_t ata_read(uint8_t bus, uint8_t drive, uint32_t lba,
                     uint8_t count, uint16_t* buf);

/**
 * @brief Writes sectors to the drive using PIO
 * @return OS_OK on success
 */
os_status_t ata_write(uint8_t bus, uint8_t drive, uint32_t lba,
                      uint8_t count, const uint16_t* buf);

#endif /* DRIVERS_ATA_H */
