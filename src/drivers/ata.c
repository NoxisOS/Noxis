/**
 * @file    drivers/ata.c
 * @brief   ATA PIO driver — read/write sectors, drive detection
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <drivers/ata.h>
#include <hal/ports.h>
#include <common/types.h>

/* ── ATA port bases ────────────────────────────────────────── */
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_SECONDARY_BASE  0x170
#define ATA_DATA(base)      ((base) + 0)   /* 0x1F0/0x170 */
#define ATA_ERROR(base)     ((base) + 1)   /* read: error, write: features */
#define ATA_COUNT(base)     ((base) + 2)   /* sector count */
#define ATA_LBA_LO(base)    ((base) + 3)
#define ATA_LBA_MID(base)   ((base) + 4)
#define ATA_LBA_HI(base)    ((base) + 5)
#define ATA_DRIVE(base)     ((base) + 6)   /* drive/head */
#define ATA_STATUS(base)    ((base) + 7)   /* read: status, write: command */

/* ── status register bits ──────────────────────────────────── */
#define ATA_BSY   0x80
#define ATA_DRDY  0x40
#define ATA_DRQ   0x08
#define ATA_ERR   0x01

/* ── commands ──────────────────────────────────────────────── */
#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_IDENTIFY 0xEC

static uint32_t _timeout(uint16_t base) {
    for (uint32_t t = 0; t < 1000000; t++) {
        uint8_t st = port_byte_in(ATA_STATUS(base));
        if (!(st & ATA_BSY)) {
            if (st & ATA_DRDY) return 1;
            if (st & ATA_ERR)  return 0;
        }
        io_delay();
    }
    return 0;
}

static uint32_t _wait_drq(uint16_t base) {
    for (uint32_t t = 0; t < 1000000; t++) {
        uint8_t st = port_byte_in(ATA_STATUS(base));
        if (st & ATA_ERR) return 0;
        if (st & ATA_DRQ) return 1;
        io_delay();
    }
    return 0;
}

static uint16_t _ata_base(uint8_t bus) {
    return (bus == ATA_PRIMARY) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
}

/* ── public functions ──────────────────────────────────────── */

os_status_t ata_init(uint8_t bus, uint8_t drive) {
    uint16_t base = _ata_base(bus);
    uint8_t  drv  = 0xA0 | (uint8_t)(drive << 4);
    port_byte_out(ATA_DRIVE(base), drv);
    for (uint32_t i = 0; i < 4; i++) io_delay();
    if (!_timeout(base)) return OS_ERR_NOT_FOUND;
    return OS_OK;
}

os_status_t ata_read(uint8_t bus, uint8_t drive, uint32_t lba,
                     uint8_t count, uint16_t* buf) {
    if (!buf || count == 0) return OS_ERR_INVALID;

    uint16_t base = _ata_base(bus);
    uint8_t  drv  = 0xE0 | (uint8_t)(drive << 4) | (uint8_t)((lba >> 24) & 0x0F);

    port_byte_out(ATA_DRIVE(base), drv);
    for (uint32_t i = 0; i < 4; i++) io_delay();
    if (!_timeout(base)) return OS_ERR_IO;

    port_byte_out(ATA_ERROR(base), 0);
    port_byte_out(ATA_COUNT(base), count);
    port_byte_out(ATA_LBA_LO(base),  (uint8_t)(lba & 0xFF));
    port_byte_out(ATA_LBA_MID(base), (uint8_t)((lba >> 8) & 0xFF));
    port_byte_out(ATA_LBA_HI(base),  (uint8_t)((lba >> 16) & 0xFF));
    port_byte_out(ATA_STATUS(base), ATA_CMD_READ);

    for (uint8_t s = 0; s < count; s++) {
        if (!_wait_drq(base)) return OS_ERR_IO;
        for (uint32_t i = 0; i < 256; i++) {
            buf[s * 256 + i] = port_word_in(ATA_DATA(base));
        }
    }

    return OS_OK;
}

os_status_t ata_write(uint8_t bus, uint8_t drive, uint32_t lba,
                      uint8_t count, const uint16_t* buf) {
    if (!buf || count == 0) return OS_ERR_INVALID;

    uint16_t base = _ata_base(bus);
    uint8_t  drv  = 0xE0 | (uint8_t)(drive << 4) | (uint8_t)((lba >> 24) & 0x0F);

    port_byte_out(ATA_DRIVE(base), drv);
    for (uint32_t i = 0; i < 4; i++) io_delay();
    if (!_timeout(base)) return OS_ERR_IO;

    port_byte_out(ATA_ERROR(base), 0);
    port_byte_out(ATA_COUNT(base), count);
    port_byte_out(ATA_LBA_LO(base),  (uint8_t)(lba & 0xFF));
    port_byte_out(ATA_LBA_MID(base), (uint8_t)((lba >> 8) & 0xFF));
    port_byte_out(ATA_LBA_HI(base),  (uint8_t)((lba >> 16) & 0xFF));
    port_byte_out(ATA_STATUS(base), ATA_CMD_WRITE);

    for (uint8_t s = 0; s < count; s++) {
        if (!_wait_drq(base)) return OS_ERR_IO;
        for (uint32_t i = 0; i < 256; i++) {
            port_word_out(ATA_DATA(base), buf[s * 256 + i]);
        }
    }

    return OS_OK;
}
