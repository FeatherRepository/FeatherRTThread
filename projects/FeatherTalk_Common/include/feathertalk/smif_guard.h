#ifndef FEATHERTALK_SMIF_GUARD_H
#define FEATHERTALK_SMIF_GUARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SMIF0 is shared by both application cores: M33 executes its image from the
 * S25FS128S while M55 also uses the last 2 MiB as a filesystem.  A program or
 * erase command therefore requires the M33 to execute exclusively from SRAM.
 *
 * The guard occupies the final cache-line pair of the M55-owned shared SRAM
 * page.  Each core uses its natural alias for the same physical storage.
 */
#define FEATHERTALK_SMIF_GUARD_M55_ADDR       0x240FFFC0UL
/* This page is allocated from the M55-owned cross-core SRAM window.  Both
 * cores must therefore use its common S-bus address; the 0x04000000 C-bus
 * alias is not the writable cross-core view for M33. */
#define FEATHERTALK_SMIF_GUARD_M33_ADDR       0x240FFFC0UL
#define FEATHERTALK_SMIF_GUARD_BYTES          64U
#define FEATHERTALK_SMIF_GUARD_CACHELINE      32U
#define FEATHERTALK_SMIF_GUARD_MAGIC          0x46544744UL /* "FTGD" */
#define FEATHERTALK_SMIF_GUARD_VERSION        1UL

typedef enum
{
    FEATHERTALK_SMIF_OP_NONE    = 0,
    FEATHERTALK_SMIF_OP_PROGRAM = 1,
    FEATHERTALK_SMIF_OP_ERASE   = 2
} feathertalk_smif_operation_t;

/*
 * Keep the M55-owned and M33-owned words on different M55 D-cache lines.
 * M55 cleans line 0 after publishing a request and invalidates line 1 before
 * consuming an acknowledgement.  M33 has no data cache in this design and
 * accesses the C-bus alias directly.
 */
typedef struct
{
    /* Cache line 0: written only by M55. */
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t epoch;
    volatile uint32_t request_seq;
    volatile uint32_t release_seq;
    volatile uint32_t operation;
    volatile uint32_t address;
    volatile uint32_t size;

    /* Cache line 1: written only by M33. */
    volatile uint32_t ready_epoch;
    volatile uint32_t parked_seq;
    volatile uint32_t completed_seq;
    volatile uint32_t rejected_seq;
    volatile uint32_t reserved[4];
} feathertalk_smif_guard_shared_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(feathertalk_smif_guard_shared_t) == FEATHERTALK_SMIF_GUARD_BYTES,
               "SMIF guard must occupy exactly two 32-byte cache lines");
#endif

/* Strong M55 hooks override the generic weak hooks in the FAL driver. */
int feathertalk_smif_guard_init(void);
int feathertalk_smif_guard_acquire(uint32_t operation,
                                  uint32_t address,
                                  uint32_t size);
void feathertalk_smif_guard_release(int result);

/* Starts the M33 high-priority SRAM parking service. */
int feathertalk_smif_guard_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_SMIF_GUARD_H */
