// SPDX-License-Identifier: GPL-3.0-or-later
#include <switch.h>
int nx_sdmc_commit(void) {
    return R_SUCCEEDED(fsdevCommitDevice("sdmc"));
}
