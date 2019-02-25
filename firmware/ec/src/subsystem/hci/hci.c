/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */
/*****************************************************************************
 *                                HEADER FILES
 *****************************************************************************/
#include "inc/subsystem/hci/hci.h"
#include "inc/utils/util.h"
#include "src/filesystem/fs_wrapper.h"
#include <stdlib.h>
#include <string.h>
/*****************************************************************************
 *                             HANDLES DEFINITION
 *****************************************************************************/
/* Global Task Configuration Variables */

bool HCI_Init(void *driver, void *return_buf)
{
    HciBuzzer_init();
    return true;
}
#if 0
bool hci_log(void *driver, void *mSgPtr) {
    OCMPMessageFrame *pMsg = mSgPtr;
    Util_enqueueMsg(fsRxMsgQueue, semFilesysMsg,
                    (uint8_t*) pMsg);
    Semaphore_pend(semFSreadMsg, BIOS_WAIT_FOREVER);
    return true;
}
#endif
