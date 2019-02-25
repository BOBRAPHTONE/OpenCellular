/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "common/inc/global/Framework.h"
#include "helpers/memory.h"
#include "inc/common/bigbrother.h"
#include "inc/common/global_header.h"
#include "inc/subsystem/ethernet/ethernetSS.h"
#include "inc/utils/ocmp_util.h"
#include "src/filesystem/fs_wrapper.h"
#if 0
bool eth_log(void *driver, void *mSgPtr) {
    OCMPMessageFrame *pMsg = mSgPtr;
    Util_enqueueMsg(fsRxMsgQueue, semFilesysMsg,
                    (uint8_t*) pMsg);
    Semaphore_pend(semFSreadMsg, BIOS_WAIT_FOREVER);
    return true;
}
#endif
