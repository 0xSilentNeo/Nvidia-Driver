/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_INSTRUCTION_POINTER_H_
#define _LKPI_LINUX_INSTRUCTION_POINTER_H_
#define _RET_IP_ ((unsigned long)__builtin_return_address(0))
#define _THIS_IP_ ((unsigned long)__builtin_frame_address(0))
#endif
