# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

"""Tests for environment variable driven code paths in the rccl-tests binaries.

These tests verify that env-var-controlled features activate and produce the
expected output structure, without validating exact counter values (which
depend on live network hardware).
"""

import pytest
from test_runner import run_rccl_perf, run_rccl_mpi

# Minimal args shared by all tests: 1 thread, 1 GPU, single 1K message.
_BASE_ARGS = ["-t", "1", "-g", "1", "-b", "1K", "-e", "1K",
              "-d", "float", "-o", "sum", "-n", "1", "-w", "0"]


# ---------------------------------------------------------------------------
# RCCL_TESTS_NET_COUNTER_ENABLE
# ---------------------------------------------------------------------------

class TestNetCounter:
    """NIC counter collection (collector.cu / collector.h).

    Tests inspect stdout for the presence or absence of the
    NET_COUNTER_TABLE header line and the activation notice printed by
    NetCounterCollectBefore(). No real NIC or sysfs is required — the
    binary returns zero-filled counters when sysfs paths are absent.
    """

    def test_disabled_by_default(self, gpu_count):
        """Counter table must NOT appear when env var is unset."""
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS)
        assert "NET_COUNTER_TABLE" not in result.stdout
        assert "RCCL_TESTS_NET_COUNTER_ENABLE" not in result.stdout

    def test_enabled_prints_header(self, gpu_count):
        """Setting RCCL_TESTS_NET_COUNTER_ENABLE=1 prints the activation notice."""
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS,
                               env_overrides={"RCCL_TESTS_NET_COUNTER_ENABLE": "1"})
        assert "Network counter collection enabled" in result.stdout

    def test_enabled_prints_table(self, gpu_count):
        """Setting RCCL_TESTS_NET_COUNTER_ENABLE=1 prints NET_COUNTER_TABLE."""
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS,
                               env_overrides={"RCCL_TESTS_NET_COUNTER_ENABLE": "1"})
        assert "NET_COUNTER_TABLE" in result.stdout

    def test_nccl_ib_hca_device_list(self, gpu_count):
        """NCCL_IB_HCA drives the device list; binary reports it used that source."""
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS,
                               env_overrides={
                                   "RCCL_TESTS_NET_COUNTER_ENABLE": "1",
                                   "NCCL_IB_HCA": "bnxt_re0,bnxt_re1",
                               })
        assert "Device list from NCCL_IB_HCA" in result.stdout

    def test_nic_counter_list_subset(self, gpu_count):
        """RCCL_TESTS_NIC_COUNTER_LIST restricts counters to the requested subset.

        Asserts against the '# Counters (N): ...' announcement line printed by
        NetCounterCollectBefore(), which is always emitted regardless of whether
        real NICs are present. The table header may not appear on NIC-less
        systems (table prints 'NO_DATA' and returns early), so we do not assert
        against the table columns directly.
        """
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS,
                               env_overrides={
                                   "RCCL_TESTS_NET_COUNTER_ENABLE": "1",
                                   "RCCL_TESTS_NIC_COUNTER_LIST": "rx_cnp_pkts,tx_cnp_pkts",
                               })
        # Announcement line is always printed: "# Counters (2): rx_cnp_pkts tx_cnp_pkts"
        assert "rx_cnp_pkts" in result.stdout
        assert "tx_cnp_pkts" in result.stdout
        # Counter NOT in the list must not appear anywhere in stdout
        assert "rx_roce_discards" not in result.stdout

    def test_nic_counter_list_single(self, gpu_count):
        """Single-counter list is accepted; counter name appears in announcement line."""
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS,
                               env_overrides={
                                   "RCCL_TESTS_NET_COUNTER_ENABLE": "1",
                                   "RCCL_TESTS_NIC_COUNTER_LIST": "rx_cnp_pkts",
                               })
        # "# Counters (1): rx_cnp_pkts" is always printed before NIC discovery
        assert "rx_cnp_pkts" in result.stdout

    def test_nic_prefix_filter(self, gpu_count):
        """RCCL_TESTS_NET_COUNTER_NIC_PREFIX is accepted without error."""
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS,
                               env_overrides={
                                   "RCCL_TESTS_NET_COUNTER_ENABLE": "1",
                                   "RCCL_TESTS_NET_COUNTER_NIC_PREFIX": "benic",
                               })
        # Binary should not crash; table or activation notice must appear
        assert "NET_COUNTER_TABLE" in result.stdout or \
               "Network counter collection enabled" in result.stdout

    def test_zero_value_not_set_to_1(self, gpu_count):
        """RCCL_TESTS_NET_COUNTER_ENABLE=0 behaves the same as unset."""
        result = run_rccl_perf("all_reduce_perf", _BASE_ARGS,
                               env_overrides={"RCCL_TESTS_NET_COUNTER_ENABLE": "0"})
        assert "NET_COUNTER_TABLE" not in result.stdout


# ---------------------------------------------------------------------------
# NCCL_TESTS_SPLIT / NCCL_TESTS_SPLIT_MASK — GPU partitioning (MPI only)
# ---------------------------------------------------------------------------

class TestSplit:
    """GPU communicator splitting via NCCL_TESTS_SPLIT / NCCL_TESTS_SPLIT_MASK.

    Splitting uses MPI_Comm_split, so all tests require at least 2 MPI ranks.
    Tests verify the binary accepts every documented operator form without
    error (exit code 0). They do not validate the resulting group topology,
    which would require multi-node hardware.

    All tests require at least 2 MPI ranks — on a 1-GPU system MPI_Comm_split
    produces a single group, which doesn't exercise the split code path.
    """

    @pytest.mark.mpi
    @pytest.mark.parametrize("split_value", [
        "AND 0x1", "& 0x1",
        "MOD 2",   "% 2",
        "OR 0x0",  "| 0x0",
        "DIV 1",   "/ 1",
    ], ids=[
        "AND_hex", "and_sym_hex",
        "MOD_dec", "mod_sym_dec",
        "OR_hex",  "or_sym_hex",
        "DIV_dec", "div_sym_dec",
    ])
    def test_split_operator(self, split_value, gpu_count):
        """NCCL_TESTS_SPLIT with every documented operator exits cleanly."""
        if gpu_count < 2:
            pytest.skip("NCCL_TESTS_SPLIT requires at least 2 MPI ranks")
        run_rccl_mpi("all_reduce_perf", gpu_count, _BASE_ARGS,
                     env_overrides={"NCCL_TESTS_SPLIT": split_value})

    @pytest.mark.mpi
    @pytest.mark.parametrize("mask", ["0x1", "0x3", "0x7"], ids=["mask_0x1", "mask_0x3", "mask_0x7"])
    def test_split_mask(self, mask, gpu_count):
        """NCCL_TESTS_SPLIT_MASK (hex) exits cleanly."""
        if gpu_count < 2:
            pytest.skip("NCCL_TESTS_SPLIT_MASK requires at least 2 MPI ranks")
        run_rccl_mpi("all_reduce_perf", gpu_count, _BASE_ARGS,
                     env_overrides={"NCCL_TESTS_SPLIT_MASK": mask})

    @pytest.mark.mpi
    def test_split_mod_each_rank_own_group(self, gpu_count):
        """MOD <gpu_count> gives every rank its own group (1-rank communicators).

        Uses the actual GPU count so the test is valid on any machine size.
        """
        if gpu_count < 2:
            pytest.skip("NCCL_TESTS_SPLIT requires at least 2 MPI ranks")
        run_rccl_mpi("all_reduce_perf", gpu_count,
                     ["-t", "1", "-g", "1", "-b", "1K", "-e", "1K",
                      "-d", "float", "-o", "sum", "-n", "1", "-w", "0"],
                     env_overrides={"NCCL_TESTS_SPLIT": f"MOD {gpu_count}"})
