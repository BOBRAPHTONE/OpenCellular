/**
 * Copyright (c) 2017-present, Facebook, Inc
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 * This file acts as wrapper for little filesystem, contains filesystem
 * initialization, block read, block write, block erase as a main functions
 * moreover provides API's like fileRead, fs_wrapper_data_write for external
 * application to read and write data to at45db flash memory by using SPI
 * interface.
 */

#include "Board.h"
#include "common/inc/global/Framework.h"
#include "common/inc/global/ocmp_frame.h"
#include "inc/common/bigbrother.h"
#include "inc/common/global_header.h"
#include "inc/devices/at45db.h"
#include <inc/global/OC_CONNECT1.h>
#include "inc/utils/ocmp_util.h"
#include "inc/utils/util.h"
#include <string.h>
#include <stdlib.h>
#include "src/filesystem/lfs.h"
#include "src/filesystem/fs_wrapper.h"
#include <ti/sysbios/BIOS.h>
#include <ti/drivers/GPIO.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Queue.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/SPI.h>
#include "src/registry/SSRegistry.h"

#define AT45DB_STATUS_READY 0xBC
#define BLOCK_SIZE 256
#define BLOCK_COUNT 32768

#define FILE_SIZE_LIMIT 8192
#define LOOK_AHEAD 256
#define NEXT_MSG_FLAG 1
#define PAGE_SIZE 256
#define PAYLOAD_SIZE 47
#define READ_SIZE 256
#define WRITE_SIZE 256

static Queue_Struct fsRxMsg;
static Queue_Struct fsTxMsg;

lfs_t lfs;
lfs_file_t file;

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_block_device_read
 **
 **    DESCRIPTION     : This function called by filesystem to read block device
 **
 **    ARGUMENTS       : context for device configuration, block or page number,
 **
 **                      block or page offset, data buffer, size of data to be
 *read
 **
 **    RETURN TYPE     : Success or failure
 **
 *****************************************************************************/
int fs_wrapper_block_device_read(const struct lfs_config *cfg,
                                 lfs_block_t block, lfs_offset_t offset,
                                 void *buffer, lfs_size_t size)
{
    if (at45db_data_read(cfg->context, buffer, size, offset, block) !=
        RETURN_OK) {
        return LFS_ERR_IO;
    }

    return LFS_ERR_OK;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_block_device_write
 **
 **    DESCRIPTION     : This function called by filesystem to write block
 *device
 **
 **    ARGUMENTS       : context for device configuration, block or page number,
 **
 **                      block or page offset, data buffer, size of data to be
 *written
 **
 **    RETURN TYPE     : Success or failure
 **
 *****************************************************************************/
int fs_wrapper_block_device_write(const struct lfs_config *cfg,
                                  lfs_block_t block, lfs_offset_t off,
                                  void *buffer, lfs_size_t size)
{
    if (at45db_data_write(cfg->context, buffer, size, off, block) !=
        RETURN_OK) {
        return LFS_ERR_IO;
    }

    return LFS_ERR_OK;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_block_device_erase
 **
 **    DESCRIPTION     : This function called by filesystem to erase block
 *device
 **
 **    ARGUMENTS       : context for device configuration, block or page number,
 **
 **    RETURN TYPE     : Success or failure
 **
 *****************************************************************************/
int fs_wrapper_block_device_erase(const struct lfs_config *cfg,
                                  lfs_block_t block)
{
    if (at45db_erasePage(cfg->context, block) != RETURN_OK) {
        return LFS_ERR_IO;
    }

    return LFS_ERR_OK;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_block_device_sync
 **
 **    DESCRIPTION     : This function called by filesystem to sync with block
 *device
 **
 **    ARGUMENTS       : context for device configuration
 **
 **    RETURN TYPE     : Success or failure
 **
 *****************************************************************************/
int fs_wrapper_block_device_sync(const struct lfs_config *cfg)
{
    while (!(AT45DB_STATUS_READY & at45db_readStatusRegister(cfg->context)))
        ;

    return LFS_ERR_OK;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_get_fileSize
 **
 **    DESCRIPTION     : Returns size of saved file
 **
 **    ARGUMENTS       : Path or file name
 **
 **    RETURN TYPE     : file size
 **
 *****************************************************************************/
int fs_wrapper_get_fileSize(const char *path)
{
    uint32_t fileSize = 0;

    if (lfs_file_open(&lfs, &file, path, LFS_O_RDONLY) == LFS_ERR_OK) {
        LOGGER_DEBUG("FS:: File open successfully \n");
        fileSize = lfs_file_size(&lfs, &file);
        lfs_file_close(&lfs, &file);
    }
    return fileSize;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_flashMemory_read
 **
 **    DESCRIPTION     : Reads saved logs from at45db flash memeory and enqueue
 **
 **    ARGUMENTS       : Subsystem number, filename, file size
 **
 **    RETURN TYPE     : Success or failure
 **
 *****************************************************************************/
void fs_wrapper_flashMemory_read(OCMPSubsystem subsystem, const char *fileName,
                                 uint32_t file_size, uint8_t fileIndex)
{
    uint32_t numOfMsg = 0;
    uint8_t *logFile;
    uint8_t *logFilePtr;
    OCMPMessageFrame *tMsg;

    if (file_size > 0) {
        tMsg = (OCMPMessageFrame *)OCMP_mallocFrame(PAYLOAD_SIZE);
        logFile = (uint8_t *)calloc(file_size, sizeof(uint8_t));
        if ((tMsg != NULL) && (logFile != NULL)) {
            logFilePtr = logFile;
            numOfMsg = file_size / FRAME_SIZE;
            fs_wrapper_file_read(fileName, logFile, file_size);
            while (numOfMsg) {
                logFile[NEXT_MSG_FLAG_POS] = NEXT_MSG_FLAG;
                if ((numOfMsg == LAST_MSG) && (fileIndex == 0)) {
                    logFile[NEXT_MSG_FLAG_POS] = LAST_MSG_FLAG;
                }
                if (subsystem == OC_SS_SYS) {
                    logFile[FS_OCMP_MSGTYPE_POS] = OCMP_MSG_TYPE_COMMAND;
                    memcpy(tMsg, logFile, FRAME_SIZE);
                    Util_enqueueMsg(bigBrotherTxMsgQueue, semBigBrotherMsg,
                                    (uint8_t *)tMsg);
                } else {
                    if (logFile[FS_OCMP_SUBSYSTEM_POS] == subsystem) {
                        logFile[FS_OCMP_MSGTYPE_POS] = OCMP_MSG_TYPE_COMMAND;
                        memcpy(tMsg, logFile, FRAME_SIZE);
                        Util_enqueueMsg(bigBrotherTxMsgQueue, semBigBrotherMsg,
                                        (uint8_t *)tMsg);
                    }
                }
                logFile += FRAME_SIZE;
                numOfMsg--;
            }
            free(tMsg);
            logFile = logFilePtr;
            free(logFile);
        }
    }
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_data_read
 **
 **    DESCRIPTION     : Called by subsystems and passes filename to alert_msg
 **
 **    ARGUMENTS       : Subsystem number
 **
 **    RETURN TYPE     : Success or failure
 **
 *****************************************************************************/
bool fs_wrapper_data_read(FILESystemStruct *fileSysStruct)
{
    uint8_t index = fileSysStruct->noOfFiles - 1;
    OCMPMessageFrame *tempMsg = (OCMPMessageFrame *)fileSysStruct->pMsg;
    OCMPSubsystem subsys = tempMsg->message.subsystem;
    char fileName[FS_STR_SIZE] = { 0 };
    file.cache.buffer =
        0; /* make buffer zero to avoid fail, might be a bug with filesystem */
    while (index > 0) {
        sprintf(fileName, "%s_%d", fileSysStruct->fileName, index);
        fs_wrapper_flashMemory_read(subsys, fileName,
                                    fs_wrapper_get_fileSize(fileName), index);
        index--;
    }
    fs_wrapper_flashMemory_read(
        subsys, fileSysStruct->fileName,
        fs_wrapper_get_fileSize(fileSysStruct->fileName), index);
    return true;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_data_write
 **
 **    DESCRIPTION     : It write data to specified file
 **
 **    ARGUMENTS       : Path or file name, pointer to data, data length or size
 **
 **    RETURN TYPE     : true or flase
 **
 *****************************************************************************/
#if 0
bool fs_wrapper_data_write(OCMPMessageFrame *pMsg, const char *path1, const char *path2, uint32_t size)
{
    if(fileSize(path1)>4096) {
       if(lfs_file_open(&lfs, &file, path2, LFS_O_RDWR |  LFS_O_APPEND) == LFS_ERR_OK) {
          if(lfs_file_truncate(&lfs, &file, 0) == LFS_ERR_OK) {
             if(lfs_file_close(&lfs, &file)== LFS_ERR_OK) {
                 if(lfs_rename(&lfs, path2, "logs1") == LFS_ERR_OK) {
                    if(lfs_rename(&lfs, path1, path2) == LFS_ERR_OK) {
                       if(lfs_rename(&lfs, "logs1", path1) == LFS_ERR_OK) {
                       }
                    }
                 }
             }
          }
       } else {
           LOGGER_DEBUG("FS:: File open failed \n");
       }
    }

    if(lfs_file_open(&lfs, &file, path1, LFS_O_RDWR | LFS_O_APPEND) == LFS_ERR_OK) {
       LOGGER_DEBUG("FS:: File open successfully \n");
       if(lfs_file_write(&lfs, &file, pMsg, size) == size) {
          LOGGER_DEBUG("FS:: File written successfully \n");
          if(lfs_file_close(&lfs, &file) == LFS_ERR_OK){
             LOGGER_DEBUG("FS:: File closed successfully \n");
          }
       }
    } else {
        LOGGER_DEBUG("FS:: File open failed \n");
    }
    return true;
}
#endif

bool fs_wrapper_data_write(FILESystemStruct *fileSysStruct)
{
    uint8_t index = fileSysStruct->noOfFiles - 1;
    uint8_t fileIndex = 0;
    char tempFileName1[FS_STR_SIZE] = { 0 };
    char tempFileName2[FS_STR_SIZE] = { 0 };

    if (fs_wrapper_get_fileSize(fileSysStruct->fileName) >
        fileSysStruct->maxFileSize) {
        while (index > 0) {
            sprintf(tempFileName2, "%s_%d", fileSysStruct->fileName, index);
            if (lfs_file_open(&lfs, &file, tempFileName2,
                              LFS_O_RDWR | LFS_O_APPEND) == LFS_ERR_OK) {
                if (lfs_file_truncate(&lfs, &file, 0) == LFS_ERR_OK) {
                    if (lfs_file_close(&lfs, &file) == LFS_ERR_OK) {
                        lfs_rename(&lfs, tempFileName2, "tempOrigFileName");
                        fileIndex = index;
                        while (fileIndex > 0) {
                            sprintf(tempFileName1, "%s_%d",
                                    fileSysStruct->fileName, fileIndex - 1);
                            if (lfs_rename(&lfs, tempFileName1,
                                           tempFileName2) == LFS_ERR_OK) {
                                fileIndex--;
                            }
                            sprintf(tempFileName2, "%s_%d",
                                    fileSysStruct->fileName, fileIndex);
                        }
                        if (lfs_rename(&lfs, "tempOrigFileName",
                                       fileSysStruct->fileName) == LFS_ERR_OK) {
                            break;
                        }
                    } else {
                        LOGGER_DEBUG("FS:: File close failed \n");
                    }
                }
            } else {
                LOGGER_DEBUG("FS:: File open failed \n");
            }
            index--;
        }
    }
    if (lfs_file_open(&lfs, &file, fileSysStruct->fileName,
                      LFS_O_RDWR | LFS_O_APPEND) == LFS_ERR_OK) {
        LOGGER_DEBUG("FS:: File open successfully \n");
        if (lfs_file_write(&lfs, &file, fileSysStruct->pMsg,
                           fileSysStruct->frameSize) ==
            fileSysStruct->frameSize) {
            LOGGER_DEBUG("FS:: File write successfully \n");
            if (lfs_file_close(&lfs, &file) == LFS_ERR_OK) {
                LOGGER_DEBUG("FS:: File closed successfully \n");
            } else {
                LOGGER_DEBUG("FS:: File closed failed \n");
            }
        } else {
            LOGGER_DEBUG("FS:: File write failed \n");
        }
    } else {
        LOGGER_DEBUG("FS:: File open failed \n");
    }
    return true;
}
/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_file_read
 **
 **    DESCRIPTION     : It reads data from specified file
 **
 **    ARGUMENTS       : Path or file name, pointer to data, data length or size
 **
 **    RETURN TYPE     : true or flase
 **
 *****************************************************************************/
bool fs_wrapper_file_read(const char *fileName, uint8_t *buf, uint32_t size)
{
    if (lfs_file_open(&lfs, &file, fileName, LFS_O_RDWR) == LFS_ERR_OK) {
        LOGGER_DEBUG("FS:: File open successfully \n");
        if (lfs_file_read(&lfs, &file, buf, size) == size) {
            LOGGER_DEBUG("FS:: File read successfully \n");
            if (lfs_file_close(&lfs, &file) == LFS_ERR_OK) {
                LOGGER_DEBUG("FS:: File closed successfully \n");
            } else {
                LOGGER_DEBUG("FS:: File closed failed \n");
            }
        } else {
            LOGGER_DEBUG("FS:: File read failed \n");
        }
    } else {
        LOGGER_DEBUG("FS:: File open failed \n");
    }
    return true;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_msgHandler
 **
 **    DESCRIPTION     : It calls fs_wrapper_data_write function to write data
 *by passing file name
 **
 **    ARGUMENTS       : data pointer
 **
 **    RETURN TYPE     : true or flase
 **
 *****************************************************************************/
static bool fs_wrapper_msgHandler(FILESystemStruct *fileSysStruct)
{
    switch (fileSysStruct->operation) {
        case WRITE_FLAG:
            fs_wrapper_data_write(fileSysStruct);
            Semaphore_post(semFSwriteMsg);
            break;
        case READ_FLAG:
            fs_wrapper_data_read(fileSysStruct);
            Semaphore_post(semFSreadMsg);
            break;
        default:
            break;
    }
#if 0
    if(fileSysStruct->operation == WRITE_FLAG) {
       fs_wrapper_data_write(fileSysStruct);
       Semaphore_post(semFSwriteMsg);
    }

    if(fileSysStruct->operation == READ_FLAG) {

    }
#endif
    return true;
}

/*****************************************************************************
 **    FUNCTION NAME   : fs_wrapper_fileSystem_init
 **
 **    DESCRIPTION     : It initializes filesystem by mounting device
 **
 **    ARGUMENTS       : arg0 for SPI device configuration, arg1 for return
 *argument
 **
 **    RETURN TYPE     : true or flase
 **
 *****************************************************************************/
void fs_wrapper_fileSystem_init(UArg arg0, UArg arg1)
{
    uint8_t index = 0;
    FILESystemStruct *fileSysStruct;
    memset(&lfs, 0, sizeof(lfs));
    memset(&file, 0, sizeof(file));

    /*configuration of the filesystem is provided by this struct */
    const struct lfs_config cfg = {
        .context = (void *)arg0,
        .read = fs_wrapper_block_device_read,
        .prog = fs_wrapper_block_device_write,
        .erase = fs_wrapper_block_device_erase,
        .sync = fs_wrapper_block_device_sync,
        .read_size = READ_SIZE,
        .prog_size = WRITE_SIZE,
        .block_size = BLOCK_SIZE,
        .block_count = BLOCK_COUNT,
        .lookahead = LOOK_AHEAD,
    };

    int err = lfs_mount(&lfs, &cfg);

    if (err) {
        lfs_format(&lfs, &cfg);
        err = lfs_mount(&lfs, &cfg);
    }

    if (!err) {
        LOGGER_DEBUG("FS:: Filesystem mounted successfully \n");

#if 0
    while (index < ((AT45DB_Dev *)arg0)->cfg.noOfFiles) {
    	if(lfs_file_open(&lfs, &file, ((AT45DB_Dev *)arg0)->cfg.fileName[index], LFS_O_CREAT | LFS_O_EXCL) == LFS_ERR_OK) {
    		LOGGER_DEBUG("FS:: File created successfully in flash(at45db) memory \n");
    		if(lfs_file_close(&lfs, &file) == LFS_ERR_OK) {
    			LOGGER_DEBUG("FS:: File closed successfully \n");
    		}
    	} else {
    		LOGGER_DEBUG("FS:: File already exist in flash(at45db) memory \n");
    	}
    	index++;
    }
#endif
        while (true) {
            if (Semaphore_pend(semFilesysMsg, BIOS_WAIT_FOREVER)) {
                while (!Queue_empty(fsRxMsgQueue)) {
                    fileSysStruct =
                        (FILESystemStruct *)Util_dequeueMsg(fsRxMsgQueue);
                    if (fileSysStruct != NULL) {
                        if (!fs_wrapper_msgHandler(fileSysStruct)) {
                            LOGGER_ERROR("ERROR:: Unable to route message \n");
                            free(fileSysStruct);
                        }
                    }
                }
            }
        }
    }
}
