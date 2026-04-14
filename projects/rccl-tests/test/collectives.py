# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class Collective:
    name: str
    executable: str
    has_ops: bool
    has_root: bool
    arch_gate: Optional[tuple] = None  # e.g., ("gfx942", "gfx950")


COLLECTIVES = [
    Collective("allreduce",      "all_reduce_perf",      has_ops=True,  has_root=False),
    Collective("allgather",      "all_gather_perf",      has_ops=False, has_root=False),
    Collective("broadcast",      "broadcast_perf",       has_ops=False, has_root=True),
    Collective("reduce",         "reduce_perf",          has_ops=True,  has_root=True),
    Collective("reducescatter",  "reduce_scatter_perf",  has_ops=True,  has_root=False),
    Collective("alltoall",       "alltoall_perf",        has_ops=False, has_root=False),
    Collective("alltoallv",      "alltoallv_perf",       has_ops=False, has_root=False),
    Collective("scatter",        "scatter_perf",         has_ops=False, has_root=True),
    Collective("gather",         "gather_perf",          has_ops=False, has_root=True),
    Collective("sendrecv",       "sendrecv_perf",        has_ops=False, has_root=False),
    Collective("hypercube",      "hypercube_perf",       has_ops=False, has_root=False),
    Collective("allreduce_bias", "all_reduce_bias_perf", has_ops=True,  has_root=False,
               arch_gate=("gfx942", "gfx950")),
]

# Convenience subsets for parametrize
COLLECTIVES_WITH_OPS  = [c for c in COLLECTIVES if c.has_ops]
COLLECTIVES_WITH_ROOT = [c for c in COLLECTIVES if c.has_root]
COLLECTIVE_IDS        = [c.name for c in COLLECTIVES]

# Representative message sizes for lifecycle / smoke tests
MSG_SIZES = ["1K", "1M", "1G"]

# Full op and datatype lists (used by test_ops.py and test_dtypes.py)
OPS = ["sum", "prod", "min", "max", "avg", "mulsum"]
DATATYPES = [
    "int8", "uint8", "int32", "uint32", "int64", "uint64",
    "half", "float", "double", "bfloat16", "fp8_e4m3", "fp8_e5m2",
]
