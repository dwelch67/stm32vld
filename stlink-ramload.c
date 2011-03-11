/*
 Copyright (c) 2010 "Capt'ns Missing Link" Authors. All rights reserved.
 Use of this source code is governed by a BSD-style
 license that can be found in the LICENSE file.

 A linux stlink access demo. The purpose of this file is to mitigate the usual
 "reinventing the wheel" force by incompatible licenses and give you an idea,
 how to access the stlink device. That doesn't mean you should be a free-loader
 and not contribute your improvements to this code.

 Author: Martin Capitanio <m@capitanio.org>
 The stlink related constants kindly provided by Oliver Spencer (OpenOCD)
 for use in a GPL compatible license.

 Notes:
 gcc -O0 -g3 -Wall -c -std=gnu99 -o stlink-access-test.o stlink-access-test.c
 gcc  -o stlink-access-test stlink-access-test.o -lsgutils2

 Code format ~ TAB = 8, K&R, linux kernel source, golang oriented
 Tested compatibility: linux, gcc >= 4.3.3

 The communication is based on standard USB mass storage device
 BOT (Bulk Only Transfer)
 - Endpoint 1: BULK_IN, 64 bytes max
 - Endpoint 2: BULK_OUT, 64 bytes max

 All CBW transfers are ordered with the LSB (byte 0) first (little endian).
 Any command must be answered before sending the next command.
 Each USB transfer must complete in less than 1s.

 SB Device Class Definition for Mass Storage Devices:
 www.usb.org/developers/devclass_docs/usbmassbulk_10.pdf

 dt     - Data Transfer (IN/OUT)
 CBW        - Command Block Wrapper
 CSW        - Command Status Wrapper
 RFU        - Reserved for Future Use
 scsi_pt    - SCSI pass-through
 sg     - SCSI generic

 * usb-storage.quirks
 http://git.kernel.org/?p=linux/kernel/git/torvalds/linux-2.6.git;a=blob_plain;f=Documentation/kernel-parameters.txt
 Each entry has the form VID:PID:Flags where VID and PID are Vendor and Product
 ID values (4-digit hex numbers) and Flags is a set of characters, each corresponding
 to a common usb-storage quirk flag as follows:

 a = SANE_SENSE (collect more than 18 bytes of sense data);
 b = BAD_SENSE (don't collect more than 18 bytes of sense data);
 c = FIX_CAPACITY (decrease the reported device capacity by one sector);
 h = CAPACITY_HEURISTICS (decrease the reported device capacity by one sector if the number is odd);
 i = IGNORE_DEVICE (don't bind to this device);
 l = NOT_LOCKABLE (don't try to lock and unlock ejectable media);
 m = MAX_SECTORS_64 (don't transfer more than 64 sectors = 32 KB at a time);
 o = CAPACITY_OK (accept the capacity reported by the device);
 r = IGNORE_RESIDUE (the device reports bogus residue values);
 s = SINGLE_LUN (the device has only one Logical Unit);
 w = NO_WP_DETECT (don't test whether the medium is write-protected).

 Example: quirks=0419:aaf5:rl,0421:0433:rc
 http://permalink.gmane.org/gmane.linux.usb.general/35053

 modprobe -r usb-storage && modprobe usb-storage quirks=483:3744:l

 Equivalently, you can add a line saying

 options usb-storage quirks=483:3744:l

 to your /etc/modprobe.conf or /etc/modprobe.d/local.conf (or add the "quirks=..."
 part to an existing options line for usb-storage).
 */

/*
Modified by dwelch@dwelch.com see stlink-access-test-04.tgz or
capitanio.org for original source.
*/

#define __USE_GNU
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// sgutils2 (apt-get install libsgutils2-dev)
#include <scsi/sg_lib.h>
#include <scsi/sg_pt.h>

// device access
#define RDWR        0
#define RO      1
#define SG_TIMEOUT_SEC  1 // actually 1 is about 2 sec
// Each CDB can be a total of 6, 10, 12, or 16 bytes, later version
// of the SCSI standard also allow for variable-length CDBs (min. CDB is 6).
// the stlink needs max. 10 bytes.
#define CDB_6       6
#define CDB_10      10
#define CDB_12      12
#define CDB_16      16

#define CDB_SL      10

// Query data flow direction.
#define Q_DATA_OUT  0
#define Q_DATA_IN   1

// The SCSI Request Sense command is used to obtain sense data
// (error information) from a target device.
// http://en.wikipedia.org/wiki/SCSI_Request_Sense_Command
#define SENSE_BUF_LEN       32

// Max data transfer size.
// 6kB = max mem32_read block, 8kB sram
//#define Q_BUF_LEN 96
#define Q_BUF_LEN   1024 * 100

// st-link vendor cmd's
#define USB_ST_VID          0x0483
#define USB_STLINK_PID          0x3744

// STLINK_DEBUG_RESETSYS, etc:
#define STLINK_OK           0x80
#define STLINK_FALSE            0x81
#define STLINK_CORE_RUNNINIG        0x80
#define STLINK_CORE_HALTED      0x81
#define STLINK_CORE_STAT_UNKNOWN    -1

#define STLINK_GET_VERSION      0xf1
#define STLINK_GET_CURRENT_MODE 0xf5

#define STLINK_DEBUG_COMMAND        0xF2
#define STLINK_DFU_COMMAND      0xF3
#define STLINK_DFU_EXIT     0x07

// STLINK_GET_CURRENT_MODE
#define STLINK_DEV_DFU_MODE     0x00
#define STLINK_DEV_MASS_MODE        0x01
#define STLINK_DEV_DEBUG_MODE       0x02
#define STLINK_DEV_UNKNOWN_MODE -1

// jtag mode cmds
#define STLINK_DEBUG_ENTER      0x20
#define STLINK_DEBUG_EXIT       0x21
#define STLINK_DEBUG_READCOREID 0x22
#define STLINK_DEBUG_GETSTATUS      0x01
#define STLINK_DEBUG_FORCEDEBUG 0x02
#define STLINK_DEBUG_RESETSYS       0x03
#define STLINK_DEBUG_READALLREGS    0x04
#define STLINK_DEBUG_READREG        0x05
#define STLINK_DEBUG_WRITEREG       0x06
#define STLINK_DEBUG_READMEM_32BIT  0x07
#define STLINK_DEBUG_WRITEMEM_32BIT 0x08
#define STLINK_DEBUG_RUNCORE        0x09
#define STLINK_DEBUG_STEPCORE       0x0a
#define STLINK_DEBUG_SETFP      0x0b
#define STLINK_DEBUG_WRITEMEM_8BIT  0x0d
#define STLINK_DEBUG_CLEARFP        0x0e
#define STLINK_DEBUG_WRITEDEBUGREG  0x0f
#define STLINK_DEBUG_ENTER_SWD      0xa3
#define STLINK_DEBUG_ENTER_JTAG 0x00

typedef struct {
    uint32_t r[16];
    uint32_t xpsr;
    uint32_t main_sp;
    uint32_t process_sp;
    uint32_t rw;
    uint32_t rw2;
} reg;

struct stlink {
    int sg_fd;
    int do_scsi_pt_err;
    // sg layer verboseness: 0 for no debug info, 10 for lots
    int verbose;

    unsigned char cdb_cmd_blk[CDB_SL];

    // Data transferred from or to device
    unsigned char q_buf[Q_BUF_LEN];
    int q_len;
    int q_data_dir; // Q_DATA_IN, Q_DATA_OUT
    // the start of the query data in the device memory space
    uint32_t q_addr;

    // Sense (error information) data
    unsigned char sense_buf[SENSE_BUF_LEN];

    uint32_t st_vid;
    uint32_t stlink_pid;
    uint32_t stlink_v;
    uint32_t jtag_v;
    uint32_t swim_v;
    uint32_t core_id;

    reg reg;
    int core_stat;
};

static void D(struct stlink *sl, char *txt) {
    if (sl->verbose > 1)
        fputs(txt, stderr);
}

// Suspends execution of the calling process for
// (at least) ms milliseconds.
static void delay(int ms) {
    fprintf(stderr, "*** wait %d ms\n", ms);
    usleep(1000 * ms);
}

// Endianness
// http://www.ibm.com/developerworks/aix/library/au-endianc/index.html
// const int i = 1;
// #define is_bigendian() ( (*(char*)&i) == 0 )
static inline unsigned int is_bigendian(void) {
    static volatile const unsigned int i = 1;
    return *(volatile const char*) &i == 0;
}

static void write_uint32(unsigned char* buf, uint32_t ui) {
    if (!is_bigendian()) { // le -> le (don't swap)
        buf[0] = ((unsigned char*) &ui)[0];
        buf[1] = ((unsigned char*) &ui)[1];
        buf[2] = ((unsigned char*) &ui)[2];
        buf[3] = ((unsigned char*) &ui)[3];
    } else {
        buf[0] = ((unsigned char*) &ui)[3];
        buf[1] = ((unsigned char*) &ui)[2];
        buf[2] = ((unsigned char*) &ui)[1];
        buf[3] = ((unsigned char*) &ui)[0];
    }
}

static void write_uint16(unsigned char* buf, uint16_t ui) {
    if (!is_bigendian()) { // le -> le (don't swap)
        buf[0] = ((unsigned char*) &ui)[0];
        buf[1] = ((unsigned char*) &ui)[1];
    } else {
        buf[0] = ((unsigned char*) &ui)[1];
        buf[1] = ((unsigned char*) &ui)[0];
    }
}

static uint32_t read_uint32(const unsigned char *c, const int pt) {
    uint32_t ui;
    char *p = (char *) &ui;

    if (!is_bigendian()) { // le -> le (don't swap)
        p[0] = c[pt];
        p[1] = c[pt + 1];
        p[2] = c[pt + 2];
        p[3] = c[pt + 3];
    } else {
        p[0] = c[pt + 3];
        p[1] = c[pt + 2];
        p[2] = c[pt + 1];
        p[3] = c[pt];
    }
    return ui;
}

static void clear_cdb(struct stlink *sl) {
    for (int i = 0; i < sizeof(sl->cdb_cmd_blk); i++)
        sl->cdb_cmd_blk[i] = 0;
    // set default
    sl->cdb_cmd_blk[0] = STLINK_DEBUG_COMMAND;
    sl->q_data_dir = Q_DATA_IN;
}

// E.g. make the valgrind happy.
static void clear_buf(struct stlink *sl) {
    fprintf(stderr, "*** clear_buf ***\n");
    for (int i = 0; i < sizeof(sl->q_buf); i++)
        sl->q_buf[i] = 0;

}

static struct stlink* stlink_open(const char *dev_name, const int verbose) {
    fprintf(stderr, "\n*** stlink_open [%s] ***\n", dev_name);
    int sg_fd = scsi_pt_open_device(dev_name, RDWR, verbose);
    if (sg_fd < 0) {
        fprintf(stderr, "error opening device: %s: %s\n", dev_name,
            safe_strerror(-sg_fd));
        return NULL;
    }

    struct stlink *sl = malloc(sizeof(struct stlink));
    if (sl == NULL) {
        fprintf(stderr, "struct stlink: out of memory\n");
        return NULL;
    }

    sl->sg_fd = sg_fd;
    sl->verbose = verbose;
    sl->core_stat = STLINK_CORE_STAT_UNKNOWN;
    sl->core_id = 0;
    sl->q_addr = 0;
    clear_buf(sl);
    return sl;
}

// close the device, free the allocated memory
void stlink_close(struct stlink *sl) {
    D(sl, "\n*** stlink_close ***\n");
    if (sl) {
        scsi_pt_close_device(sl->sg_fd);
        free(sl);
    }
}

//TODO rewrite/cleanup, save the error in sl
static void stlink_confirm_inq(struct stlink *sl, struct sg_pt_base *ptvp) {
    const int e = sl->do_scsi_pt_err;
    if (e < 0) {
        fprintf(stderr, "scsi_pt error: pass through os error: %s\n",
            safe_strerror(-e));
        return;
    } else if (e == SCSI_PT_DO_BAD_PARAMS) {
        fprintf(stderr, "scsi_pt error: bad pass through setup\n");
        return;
    } else if (e == SCSI_PT_DO_TIMEOUT) {
        fprintf(stderr, "  pass through timeout\n");
        return;
    }
    const int duration = get_scsi_pt_duration_ms(ptvp);
    if ((sl->verbose > 1) && (duration >= 0))
        fprintf(stderr, "      duration=%d ms\n", duration);

    // XXX stlink fw sends broken residue, so ignore it and use the known q_len
    // "usb-storage quirks=483:3744:r"
    // forces residue to be ignored and calculated, but this causes aboard if
    // data_len = 0 and by some other data_len values.

    const int resid = get_scsi_pt_resid(ptvp);
    const int dsize = sl->q_len - resid;

    const int cat = get_scsi_pt_result_category(ptvp);
    char buf[512];
    unsigned int slen;

    switch (cat) {
    case SCSI_PT_RESULT_GOOD:
        if (sl->verbose && (resid > 0))
            fprintf(stderr, "      notice: requested %d bytes but "
                "got %d bytes, ignore [broken] residue = %d\n",
                sl->q_len, dsize, resid);
        break;
    case SCSI_PT_RESULT_STATUS:
        if (sl->verbose) {
            sg_get_scsi_status_str(
                get_scsi_pt_status_response(ptvp), sizeof(buf),
                buf);
            fprintf(stderr, "  scsi status: %s\n", buf);
        }
        return;
    case SCSI_PT_RESULT_SENSE:
        slen = get_scsi_pt_sense_len(ptvp);
        if (sl->verbose) {
            sg_get_sense_str("", sl->sense_buf, slen, (sl->verbose
                > 1), sizeof(buf), buf);
            fprintf(stderr, "%s", buf);
        }
        if (sl->verbose && (resid > 0)) {
            if ((sl->verbose) || (sl->q_len > 0))
                fprintf(stderr, "    requested %d bytes but "
                    "got %d bytes\n", sl->q_len, dsize);
        }
        return;
    case SCSI_PT_RESULT_TRANSPORT_ERR:
        if (sl->verbose) {
            get_scsi_pt_transport_err_str(ptvp, sizeof(buf), buf);
            // http://tldp.org/HOWTO/SCSI-Generic-HOWTO/x291.html
            // These codes potentially come from the firmware on a host adapter
            // or from one of several hosts that an adapter driver controls.
            // The 'host_status' field has the following values:
            //  [0x07] Internal error detected in the host adapter.
            // This may not be fatal (and the command may have succeeded).
            fprintf(stderr, "  transport: %s", buf);
        }
        return;
    case SCSI_PT_RESULT_OS_ERR:
        if (sl->verbose) {
            get_scsi_pt_os_err_str(ptvp, sizeof(buf), buf);
            fprintf(stderr, "  os: %s", buf);
        }
        return;
    default:
        fprintf(stderr, "  unknown pass through result "
            "category (%d)\n", cat);
    }
}

static void stlink_q(struct stlink* sl) {
    fputs("CDB[", stderr);
    for (int i = 0; i < CDB_SL; i++)
        fprintf(stderr, " 0x%02x", (unsigned int) sl->cdb_cmd_blk[i]);
    fputs("]\n", stderr);

    // Get control command descriptor of scsi structure,
    // (one object per command!!)
    struct sg_pt_base *ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        fprintf(stderr, "construct_scsi_pt_obj: out of memory\n");
        return;
    }

    set_scsi_pt_cdb(ptvp, sl->cdb_cmd_blk, sizeof(sl->cdb_cmd_blk));

    // set buffer for sense (error information) data
    set_scsi_pt_sense(ptvp, sl->sense_buf, sizeof(sl->sense_buf));

    // Set a buffer to be used for data transferred from device
    if (sl->q_data_dir == Q_DATA_IN) {
        //clear_buf(sl);
        set_scsi_pt_data_in(ptvp, sl->q_buf, sl->q_len);
    } else {
        set_scsi_pt_data_out(ptvp, sl->q_buf, sl->q_len);
    }
    // Executes SCSI command (or at least forwards it to lower layers).
    sl->do_scsi_pt_err = do_scsi_pt(ptvp, sl->sg_fd, SG_TIMEOUT_SEC,
        sl->verbose);

    // check for scsi errors
    stlink_confirm_inq(sl, ptvp);
    // TODO recycle: clear_scsi_pt_obj(struct sg_pt_base * objp);
    destruct_scsi_pt_obj(ptvp);
}

static void stlink_print_data(struct stlink *sl) {
    if (sl->q_len <= 0 || sl->verbose < 2)
        return;
    if (sl->verbose > 2)
        fprintf(stderr, "data_len = %d 0x%x\n", sl->q_len, sl->q_len);

    for (uint32_t i = 0; i < sl->q_len; i++) {
        if (i % 16 == 0) {
            if (sl->q_data_dir == Q_DATA_OUT)
                fprintf(stderr, "\n<- 0x%08x ", sl->q_addr + i);
            else
                fprintf(stderr, "\n-> 0x%08x ", sl->q_addr + i);
        }
        fprintf(stderr, " %02x", (unsigned int) sl->q_buf[i]);
    }
    fputs("\n\n", stderr);
}

// TODO thinking, cleanup
static void stlink_parse_version(struct stlink *sl) {
    sl->st_vid = 0;
    sl->stlink_pid = 0;
    if (sl->q_len <= 0) {
        fprintf(stderr, "Error: could not parse the stlink version");
        return;
    }
    stlink_print_data(sl);
    uint32_t b0 = sl->q_buf[0]; //lsb
    uint32_t b1 = sl->q_buf[1];
    uint32_t b2 = sl->q_buf[2];
    uint32_t b3 = sl->q_buf[3];
    uint32_t b4 = sl->q_buf[4];
    uint32_t b5 = sl->q_buf[5]; //msb

    // b0 b1                       || b2 b3  | b4 b5
    // 4b        | 6b     | 6b     || 2B     | 2B
    // stlink_v  | jtag_v | swim_v || st_vid | stlink_pid

    sl->stlink_v = (b0 & 0xf0) >> 4;
    sl->jtag_v = ((b0 & 0x0f) << 2) | ((b1 & 0xc0) >> 6);
    sl->swim_v = b1 & 0x3f;
    sl->st_vid = (b3 << 8) | b2;
    sl->stlink_pid = (b5 << 8) | b4;

    if (sl->verbose < 2)
        return;

    fprintf(stderr, "st vid         = 0x%04x (expect 0x%04x)\n",
        sl->st_vid, USB_ST_VID);
    fprintf(stderr, "stlink pid     = 0x%04x (expect 0x%04x)\n",
        sl->stlink_pid, USB_STLINK_PID);
    fprintf(stderr, "stlink version = 0x%x\n", sl->stlink_v);
    fprintf(stderr, "jtag version   = 0x%x\n", sl->jtag_v);
    fprintf(stderr, "swim version   = 0x%x\n", sl->swim_v);
    if (sl->jtag_v == 0)
        fprintf(stderr,
            "    notice: the firmware doesn't support a jtag/swd interface\n");
    if (sl->swim_v == 0)
        fprintf(stderr,
            "    notice: the firmware doesn't support a swim interface\n");

}

static int stlink_mode(struct stlink *sl) {
    if (sl->q_len <= 0)
        return STLINK_DEV_UNKNOWN_MODE;

    stlink_print_data(sl);

    switch (sl->q_buf[0]) {
    case STLINK_DEV_DFU_MODE:
        fprintf(stderr, "stlink mode: dfu\n");
        return STLINK_DEV_DFU_MODE;
    case STLINK_DEV_DEBUG_MODE:
        fprintf(stderr, "stlink mode: debug (jtag or swd)\n");
        return STLINK_DEV_DEBUG_MODE;
    case STLINK_DEV_MASS_MODE:
        fprintf(stderr, "stlink mode: mass\n");
        return STLINK_DEV_MASS_MODE;
    }
    return STLINK_DEV_UNKNOWN_MODE;
}

static void stlink_stat(struct stlink *sl, char *txt) {
    if (sl->q_len <= 0)
        return;

    stlink_print_data(sl);

    switch (sl->q_buf[0]) {
    case STLINK_OK:
        fprintf(stderr, "  %s: ok\n", txt);
        return;
    case STLINK_FALSE:
        fprintf(stderr, "  %s: false\n", txt);
        return;
    default:
        fprintf(stderr, "  %s: unknown\n", txt);
    }
}

static void stlink_core_stat(struct stlink *sl) {
    if (sl->q_len <= 0)
        return;

    stlink_print_data(sl);

    switch (sl->q_buf[0]) {
    case STLINK_CORE_RUNNINIG:
        sl->core_stat = STLINK_CORE_RUNNINIG;
        fprintf(stderr, "  core status: running\n");
        return;
    case STLINK_CORE_HALTED:
        sl->core_stat = STLINK_CORE_HALTED;
        fprintf(stderr, "  core status: halted\n");
        return;
    default:
        sl->core_stat = STLINK_CORE_STAT_UNKNOWN;
        fprintf(stderr, "  core status: unknown\n");
    }
}

void stlink_version(struct stlink *sl) {
    D(sl, "\n*** stlink_version ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[0] = STLINK_GET_VERSION;
    sl->q_len = 6;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_parse_version(sl);
}

// Get stlink mode:
// STLINK_DEV_DFU_MODE || STLINK_DEV_MASS_MODE || STLINK_DEV_DEBUG_MODE
// usb dfu             || usb mass             || jtag or swd
int stlink_current_mode(struct stlink *sl) {
    D(sl, "\n*** stlink_current_mode ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[0] = STLINK_GET_CURRENT_MODE;
    sl->q_len = 2;
    sl->q_addr = 0;
    stlink_q(sl);
    return stlink_mode(sl);
}

// Exit the mass mode and enter the swd debug mode.
void stlink_enter_swd_mode(struct stlink *sl) {
    D(sl, "\n*** stlink_enter_swd_mode ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_ENTER;
    sl->cdb_cmd_blk[2] = STLINK_DEBUG_ENTER_SWD;
    sl->q_len = 0; // >0 -> aboard
    stlink_q(sl);
}

// Exit the mass mode and enter the jtag debug mode.
// (jtag is disabled in the discovery's stlink firmware)
void stlink_enter_jtag_mode(struct stlink *sl) {
    D(sl, "\n*** stlink_enter_jtag_mode ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_ENTER;
    sl->cdb_cmd_blk[2] = STLINK_DEBUG_ENTER_JTAG;
    sl->q_len = 0;
    stlink_q(sl);
}

// Exit the jtag or swd mode and enter the mass mode.
void stlink_exit_debug_mode(struct stlink *sl) {
    D(sl, "\n*** stlink_exit_debug_mode ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_EXIT;
    sl->q_len = 0; // >0 -> aboard
    stlink_q(sl);
}

// XXX kernel driver performs reset, the device temporally disappears
static void stlink_exit_dfu_mode(struct stlink *sl) {
    D(sl, "\n*** stlink_exit_dfu_mode ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[0] = STLINK_DFU_COMMAND;
    sl->cdb_cmd_blk[1] = STLINK_DFU_EXIT;
    sl->q_len = 0; // ??
    stlink_q(sl);
    /*
     [135121.844564] sd 19:0:0:0: [sdb] Unhandled error code
     [135121.844569] sd 19:0:0:0: [sdb] Result: hostbyte=DID_ERROR driverbyte=DRIVER_OK
     [135121.844574] sd 19:0:0:0: [sdb] CDB: Read(10): 28 00 00 00 10 00 00 00 08 00
     [135121.844584] end_request: I/O error, dev sdb, sector 4096
     [135121.844590] Buffer I/O error on device sdb, logical block 512
     [135130.122567] usb 6-1: reset full speed USB device using uhci_hcd and address 7
     [135130.274551] usb 6-1: device firmware changed
     [135130.274618] usb 6-1: USB disconnect, address 7
     [135130.275186] VFS: busy inodes on changed media or resized disk sdb
     [135130.275424] VFS: busy inodes on changed media or resized disk sdb
     [135130.286758] VFS: busy inodes on changed media or resized disk sdb
     [135130.292796] VFS: busy inodes on changed media or resized disk sdb
     [135130.301481] VFS: busy inodes on changed media or resized disk sdb
     [135130.304316] VFS: busy inodes on changed media or resized disk sdb
     [135130.431113] usb 6-1: new full speed USB device using uhci_hcd and address 8
     [135130.629444] usb-storage 6-1:1.0: Quirks match for vid 0483 pid 3744: 102a1
     [135130.629492] scsi20 : usb-storage 6-1:1.0
     [135131.625600] scsi 20:0:0:0: Direct-Access     STM32                          PQ: 0 ANSI: 0
     [135131.627010] sd 20:0:0:0: Attached scsi generic sg2 type 0
     [135131.633603] sd 20:0:0:0: [sdb] 64000 512-byte logical blocks: (32.7 MB/31.2 MiB)
     [135131.633613] sd 20:0:0:0: [sdb] Assuming Write Enabled
     [135131.633620] sd 20:0:0:0: [sdb] Assuming drive cache: write through
     [135131.640584] sd 20:0:0:0: [sdb] Assuming Write Enabled
     [135131.640592] sd 20:0:0:0: [sdb] Assuming drive cache: write through
     [135131.640609]  sdb:
     [135131.652634] sd 20:0:0:0: [sdb] Assuming Write Enabled
     [135131.652639] sd 20:0:0:0: [sdb] Assuming drive cache: write through
     [135131.652645] sd 20:0:0:0: [sdb] Attached SCSI removable disk
     [135131.671536] sd 20:0:0:0: [sdb] Result: hostbyte=DID_OK driverbyte=DRIVER_SENSE
     [135131.671548] sd 20:0:0:0: [sdb] Sense Key : Illegal Request [current]
     [135131.671553] sd 20:0:0:0: [sdb] Add. Sense: Logical block address out of range
     [135131.671560] sd 20:0:0:0: [sdb] CDB: Read(10): 28 00 00 00 f9 80 00 00 08 00
     [135131.671570] end_request: I/O error, dev sdb, sector 63872
     [135131.671575] Buffer I/O error on device sdb, logical block 7984
     [135131.678527] sd 20:0:0:0: [sdb] Result: hostbyte=DID_OK driverbyte=DRIVER_SENSE
     [135131.678532] sd 20:0:0:0: [sdb] Sense Key : Illegal Request [current]
     [135131.678537] sd 20:0:0:0: [sdb] Add. Sense: Logical block address out of range
     [135131.678542] sd 20:0:0:0: [sdb] CDB: Read(10): 28 00 00 00 f9 80 00 00 08 00
     [135131.678551] end_request: I/O error, dev sdb, sector 63872
     ...
     [135131.853565] end_request: I/O error, dev sdb, sector 4096
     */
}

static void stlink_core_id(struct stlink *sl) {
    D(sl, "\n*** stlink_core_id ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_READCOREID;
    sl->q_len = 4;
    sl->q_addr = 0;
    stlink_q(sl);
    sl->core_id = read_uint32(sl->q_buf, 0);
    if (sl->verbose < 2)
        return;
    stlink_print_data(sl);
    fprintf(stderr, "core_id = 0x%08x\n", sl->core_id);
}

// Arm-core reset -> halted state.
void stlink_reset(struct stlink *sl) {
    D(sl, "\n*** stlink_reset ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_RESETSYS;
    sl->q_len = 2;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_stat(sl, "core reset");
}

// Arm-core status: halted or running.
void stlink_status(struct stlink *sl) {
    D(sl, "\n*** stlink_status ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_GETSTATUS;
    sl->q_len = 2;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_core_stat(sl);
}

// Force the core into the debug mode -> halted state.
void stlink_force_debug(struct stlink *sl) {
    D(sl, "\n*** stlink_force_debug ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_FORCEDEBUG;
    sl->q_len = 2;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_stat(sl, "force debug");
}

// Read all arm-core registers.
void stlink_read_all_regs(struct stlink *sl) {
    D(sl, "\n*** stlink_read_all_regs ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_READALLREGS;
    sl->q_len = 84;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_print_data(sl);

    // 0-3 | 4-7 | ... | 60-63 | 64-67 | 68-71   | 72-75      | 76-79 | 80-83
    // r0  | r1  | ... | r15   | xpsr  | main_sp | process_sp | rw    | rw2
    for (int i = 0; i < 16; i++) {
        sl->reg.r[i] = read_uint32(sl->q_buf, 4 * i);
        if (sl->verbose > 1)
            fprintf(stderr, "r%2d = 0x%08x\n", i, sl->reg.r[i]);
    }
    sl->reg.xpsr = read_uint32(sl->q_buf, 64);
    sl->reg.main_sp = read_uint32(sl->q_buf, 68);
    sl->reg.process_sp = read_uint32(sl->q_buf, 72);
    sl->reg.rw = read_uint32(sl->q_buf, 76);
    sl->reg.rw2 = read_uint32(sl->q_buf, 80);
    if (sl->verbose < 2)
        return;

    fprintf(stderr, "xpsr       = 0x%08x\n", sl->reg.xpsr);
    fprintf(stderr, "main_sp    = 0x%08x\n", sl->reg.main_sp);
    fprintf(stderr, "process_sp = 0x%08x\n", sl->reg.process_sp);
    fprintf(stderr, "rw         = 0x%08x\n", sl->reg.rw);
    fprintf(stderr, "rw2        = 0x%08x\n", sl->reg.rw2);
}

// Read an arm-core register, the index must be in the range 0..20.
//  0  |  1  | ... |  15   |  16   |   17    |   18       |  19   |  20
// r0  | r1  | ... | r15   | xpsr  | main_sp | process_sp | rw    | rw2
void stlink_read_reg(struct stlink *sl, int r_idx) {
    D(sl, "\n*** stlink_read_reg");
    fprintf(stderr, " (%d) ***\n", r_idx);

    if (r_idx > 20 || r_idx < 0) {
        fprintf(stderr, "Error: register index must be in [0..20]\n");
        return;
    }
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_READREG;
    sl->cdb_cmd_blk[2] = r_idx;
    sl->q_len = 4;
    sl->q_addr = 0;
    stlink_q(sl);
    //  0  |  1  | ... |  15   |  16   |   17    |   18       |  19   |  20
    // 0-3 | 4-7 | ... | 60-63 | 64-67 | 68-71   | 72-75      | 76-79 | 80-83
    // r0  | r1  | ... | r15   | xpsr  | main_sp | process_sp | rw    | rw2
    stlink_print_data(sl);

    uint32_t r = read_uint32(sl->q_buf, 0);
    fprintf(stderr, "r_idx (%2d) = 0x%08x\n", r_idx, r);

    switch (r_idx) {
    case 16:
        sl->reg.xpsr = r;
        break;
    case 17:
        sl->reg.main_sp = r;
        break;
    case 18:
        sl->reg.process_sp = r;
        break;
    case 19:
        sl->reg.rw = r; //XXX ?(primask, basemask etc.)
        break;
    case 20:
        sl->reg.rw2 = r; //XXX ?(primask, basemask etc.)
        break;
    default:
        sl->reg.r[r_idx] = r;
    }
}

// Write an arm-core register. Index:
//  0  |  1  | ... |  15   |  16   |   17    |   18       |  19   |  20
// r0  | r1  | ... | r15   | xpsr  | main_sp | process_sp | rw    | rw2
void stlink_write_reg(struct stlink *sl, uint32_t reg, int idx) {
    D(sl, "\n*** stlink_write_reg ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_WRITEREG;
    //   2: reg index
    // 3-6: reg content
    sl->cdb_cmd_blk[2] = idx;
    write_uint32(sl->cdb_cmd_blk + 3, reg);
    sl->q_len = 2;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_stat(sl, "write reg");
}

// Write a register of the debug module of the core.
// XXX ?(atomic writes)
// TODO test
void stlink_write_dreg(struct stlink *sl, uint32_t reg, uint32_t addr) {
    D(sl, "\n*** stlink_write_dreg ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_WRITEDEBUGREG;
    // 2-5: address of reg of the debug module
    // 6-9: reg content
    write_uint32(sl->cdb_cmd_blk + 2, addr);
    write_uint32(sl->cdb_cmd_blk + 6, reg);
    sl->q_len = 2;
    sl->q_addr = addr;
    stlink_q(sl);
    stlink_stat(sl, "write debug reg");
}

// Force the core exit the debug mode.
void stlink_run(struct stlink *sl) {
    D(sl, "\n*** stlink_run ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_RUNCORE;
    sl->q_len = 2;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_stat(sl, "run core");
}

// Step the arm-core.
void stlink_step(struct stlink *sl) {
    D(sl, "\n*** stlink_step ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_STEPCORE;
    sl->q_len = 2;
    sl->q_addr = 0;
    stlink_q(sl);
    stlink_stat(sl, "step core");
}

// TODO test
// see Cortex-M3 Technical Reference Manual
void stlink_set_hw_bp(struct stlink *sl, int fp_nr, uint32_t addr, int fp) {
    D(sl, "\n*** stlink_set_hw_bp ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_SETFP;
    // 2:The number of the flash patch used to set the breakpoint
    // 3-6: Address of the breakpoint (LSB)
    // 7: FP_ALL (0x02) / FP_UPPER (0x01) / FP_LOWER (0x00)
    sl->q_buf[2] = fp_nr;
    write_uint32(sl->q_buf, addr);
    sl->q_buf[7] = fp;

    sl->q_len = 2;
    stlink_q(sl);
    stlink_stat(sl, "set flash breakpoint");
}

// TODO test
void stlink_clr_hw_bp(struct stlink *sl, int fp_nr) {
    D(sl, "\n*** stlink_clr_hw_bp ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_CLEARFP;
    sl->cdb_cmd_blk[2] = fp_nr;

    sl->q_len = 2;
    stlink_q(sl);
    stlink_stat(sl, "clear flash breakpoint");
}

// Read a "len" bytes to the sl->q_buf from the memory, max 6kB (6144 bytes)
void stlink_read_mem32(struct stlink *sl, uint32_t addr, uint16_t len) {
    D(sl, "\n*** stlink_read_mem32 ***\n");
    if (len % 4 != 0) { // !!! never ever: fw gives just wrong values
        fprintf(
            stderr,
            "Error: Data length doesn't have a 32 bit alignment: +%d byte.\n",
            len % 4);
        return;
    }
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_READMEM_32BIT;
    // 2-5: addr
    // 6-7: len
    write_uint32(sl->cdb_cmd_blk + 2, addr);
    write_uint16(sl->cdb_cmd_blk + 6, len);

    // data_in 0-0x40-len
    // !!! len _and_ q_len must be max 6k,
    //     i.e. >1024 * 6 = 6144 -> aboard)
    // !!! if len < q_len: 64*k, 1024*n, n=1..5  -> aboard
    //     (broken residue issue)
    sl->q_len = len;
    sl->q_addr = addr;
    stlink_q(sl);
    stlink_print_data(sl);
}

// Write a "len" bytes from the sl->q_buf to the memory, max 64 Bytes.
void stlink_write_mem8(struct stlink *sl, uint32_t addr, uint16_t len) {
    D(sl, "\n*** stlink_write_mem8 ***\n");
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_WRITEMEM_8BIT;
    // 2-5: addr
    // 6-7: len (>0x40 (64) -> aboard)
    write_uint32(sl->cdb_cmd_blk + 2, addr);
    write_uint16(sl->cdb_cmd_blk + 6, len);

    // data_out 0-len
    sl->q_len = len;
    sl->q_addr = addr;
    sl->q_data_dir = Q_DATA_OUT;
    stlink_q(sl);
    stlink_print_data(sl);
}

// Write a "len" bytes from the sl->q_buf to the memory, max Q_BUF_LEN bytes.
void stlink_write_mem32(struct stlink *sl, uint32_t addr, uint16_t len) {
    D(sl, "\n*** stlink_write_mem32 ***\n");
    if (len % 4 != 0) {
        fprintf(
            stderr,
            "Error: Data length doesn't have a 32 bit alignment: +%d byte.\n",
            len % 4);
        return;
    }
    clear_cdb(sl);
    sl->cdb_cmd_blk[1] = STLINK_DEBUG_WRITEMEM_32BIT;
    // 2-5: addr
    // 6-7: len "unlimited"
    write_uint32(sl->cdb_cmd_blk + 2, addr);
    write_uint16(sl->cdb_cmd_blk + 6, len);

    // data_out 0-0x40-...-len
    sl->q_len = len;
    sl->q_addr = addr;
    sl->q_data_dir = Q_DATA_OUT;
    stlink_q(sl);
    stlink_print_data(sl);
}

// 1) open a sg device, switch the stlink from dfu to mass mode
// 2) wait 5s until the kernel driver stops reseting the broken device
// 3) reopen the device
// 4) the device driver is now ready for a switch to jtag/swd mode
// TODO thinking, better error handling, wait until the kernel driver stops reseting the plugged-in device
struct stlink* stlink_force_open(const char *dev_name, const int verbose) {
    struct stlink *sl = stlink_open(dev_name, verbose);
    if (sl == NULL) {
        fputs("Error: could not open stlink device\n", stderr);
        return NULL;
    }
    stlink_version(sl);
    if (sl->st_vid != USB_ST_VID || sl->stlink_pid != USB_STLINK_PID) {
        fprintf(stderr, "Error: the device %s is not a stlink\n",
            dev_name);
        fprintf(stderr, "       VID: got %04x expect %04x \n",
            sl->st_vid, USB_ST_VID);
        fprintf(stderr, "       PID: got %04x expect %04x \n",
            sl->stlink_pid, USB_STLINK_PID);
        return NULL;
    }

    D(sl, "\n*** stlink_force_open ***\n");
    switch (stlink_current_mode(sl)) {
    case STLINK_DEV_MASS_MODE:
        return sl;
    case STLINK_DEV_DEBUG_MODE:
        // TODO go to mass?
        return sl;
    }
    fprintf(stderr, "\n*** switch the stlink to mass mode ***\n");
    stlink_exit_dfu_mode(sl);
    // exit the dfu mode -> the device is gone
    fprintf(stderr, "\n*** reopen the stlink device ***\n");
    stlink_close(sl);
    delay(5000);

    sl = stlink_open(dev_name, verbose);
    if (sl == NULL) {
        fputs("Error: could not open stlink device\n", stderr);
        return NULL;
    }
    // re-query device info
    stlink_version(sl);
    return sl;
}

static void mark_buf(struct stlink *sl) {
    clear_buf(sl);
    sl->q_buf[0] = 0x12;
    sl->q_buf[1] = 0x34;
    sl->q_buf[2] = 0x56;
    sl->q_buf[3] = 0x78;
    sl->q_buf[4] = 0x90;
    sl->q_buf[15] = 0x42;
    sl->q_buf[16] = 0x43;
    sl->q_buf[63] = 0x42;
    sl->q_buf[64] = 0x43;
    sl->q_buf[1024 * 6 - 1] = 0x42; //6kB
    sl->q_buf[1024 * 8 - 1] = 0x42; //8kB
}
struct stlink *sl;
void PUT32 ( unsigned int addr, unsigned int data )
{
    write_uint32(sl->q_buf, data);
    stlink_write_mem32(sl, addr, 4);
}
unsigned int GET32 ( unsigned int addr )
{
    stlink_read_mem32(sl, addr, 4);
    return(read_uint32(sl->q_buf, 0));
}

int main(int argc, char *argv[]) {
    // set scpi lib debug level: 0 for no debug info, 10 for lots
    const int scsi_verbose = 2;
    char *dev_name;
    FILE *fpbin;
    unsigned int ra;
    unsigned int rb;

    if(argc<3)
    {
        fputs(
            "\nUsage: stlink-access-test /dev/sgX filename.bin\n"
                "\n*** Notice: The stlink firmware violates the USB standard.\n"
                "*** If you plug-in the discovery's stlink, wait a several\n"
                "*** minutes to let the kernel driver swallow the broken device.\n"
                "*** Watch:\ntail -f /var/log/messages\n"
                "*** This command sequence can shorten the waiting time and fix some issues.\n"
                "*** Unplug the stlink and execute once as root:\n"
                "modprobe -r usb-storage && modprobe usb-storage quirks=483:3744:lrwsro\n\n",
            stderr);
        return EXIT_FAILURE;
    }
    dev_name = argv[1];

    fpbin=fopen(argv[2],"rb");
    if(fpbin==NULL)
    {
        fprintf(stderr,"Error opening file [%s]\n",argv[2]);
        return EXIT_FAILURE;
    }


    fputs("*** stlink access test ***\n", stderr);
    fprintf(stderr, "Using sg_lib %s : scsi_pt %s\n", sg_lib_version(),
        scsi_pt_version());

    sl = stlink_force_open(dev_name, scsi_verbose);
    if (sl == NULL)
        return EXIT_FAILURE;

    // we are in mass mode, go to swd
    stlink_enter_swd_mode(sl);
    stlink_current_mode(sl);
    stlink_core_id(sl);
    //----------------------------------------------------------------------

    stlink_status(sl);
    //stlink_force_debug(sl);
    stlink_reset(sl);
    stlink_status(sl);


//#define FLASH_BASE 0x40022000
//#define FLASH_KEYR (FLASH_BASE+0x04)
//#define FLASH_SR   (FLASH_BASE+0x0C)
//#define FLASH_CR   (FLASH_BASE+0x10)

    ////mass erase
    //PUT32(FLASH_SR,0x0034);
    //PUT32(FLASH_CR,0x0004);
    //PUT32(FLASH_CR,0x0044);
    //while(1)
    //{
        //rb=GET32(FLASH_SR);
        //if((rb&0x21)==0x20) break;
    //}
    //PUT32(FLASH_SR,0x0034);
    //PUT32(FLASH_CR,0x0000);


//----------------------------------------------------------------------
    ra=0x20000000;
    while(1)
    {
        if(fread(&rb,1,4,fpbin)==0) break;
        write_uint32(sl->q_buf, rb);
        stlink_write_mem32(sl, ra, 4);
        ra+=4;
    }
    printf("0x%08X\n",ra);
    for(ra=0x20000000;ra<0x20000010;ra+=4)
    {
        stlink_read_mem32(sl, ra, 4);
        rb = read_uint32(sl->q_buf, 0);
        printf("0x%04X 0x%04X\n",ra,rb);
    }

    for(ra=0x08000000;ra<0x08000010;ra+=4)
    {
        stlink_read_mem32(sl, ra, 4);
        rb = read_uint32(sl->q_buf, 0);
        printf("0x%04X 0x%04X\n",ra,rb);
    }

    stlink_write_reg(sl, 0x20000001, 15);
    stlink_write_reg(sl, 0x20000001, 14);
//----------------------------------------------------------------------


    stlink_run(sl);
    stlink_status(sl);
    //----------------------------------------------------------------------
    // back to mass mode, just in case ...
    stlink_exit_debug_mode(sl);
    stlink_current_mode(sl);
    stlink_close(sl);

    //fflush(stderr); fflush(stdout);
    return EXIT_SUCCESS;
}
