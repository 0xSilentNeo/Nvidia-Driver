/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_NUMA_H_
#define _LKPI_LINUX_NUMA_H_
#define NUMA_NO_NODE (-1)
#define numa_node_id() 0
#define dev_to_node(dev) NUMA_NO_NODE
#define cpu_to_node(cpu) 0
#endif
