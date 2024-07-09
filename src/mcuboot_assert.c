/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/__assert.h>

/* Workaround for Espressif HAL, which expects SOME mcuboot_assert_handler() implementation. */
void mcuboot_assert_handler(const char *file, int line, const char *func)
{
    __ASSERT_NO_MSG(false);
}
