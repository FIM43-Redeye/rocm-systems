/*************************************************************************
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "NetIbMPITestBase.hpp"

#ifdef MPI_TESTS_ENABLED

// Virtual Device Tests

TEST_F(NetIbMPITest, MakeVirtualDevice) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 devices for virtual device test";
    }

    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ncclResult_t result = MakeVirtualDevice(&vdev, &vProps);

    // Virtual device creation may or may not be supported
    if (result == ncclSuccess) {
        EXPECT_GE(vdev, 0) << "Virtual device ID should be non-negative";

        if (MPIEnvironment::world_rank == 0) {
            TEST_INFO("Created virtual device %d from physical devices 0 and 1", vdev);
        }
    }
}

TEST_F(NetIbMPITest, MakeVirtualDeviceInvalidProps) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    // Negative test: Zero devices
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 0;

    int vdev = -1;
    ncclResult_t result = MakeVirtualDevice(&vdev, &vProps);
    EXPECT_EQ(result, ncclInvalidUsage) << "Should fail with zero devices";
}

// NIC Fusion (vNIC) Tests

TEST_F(NetIbMPITest, ConnectAndTransfer_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    // Create a fused vNIC from physical devices 0 and 1.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
        << "Failed to create fused vNIC from devices 0 and 1";
    ASSERT_GE(vdev, 0);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(vdev, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 500;
    const int seed = 5000;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    // Register the buffer on each sub-device's PD.
    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        memset(buffer, 0, bufferSize);
        PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        FillHostBuffer(buffer, bufferSize, seed);
        PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    int sizes[1] = {0};
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    // Verify data integrity.
    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Received size mismatch";
        EXPECT_TRUE(VerifyHostBuffer(buffer, bufferSize, seed)) << "Data validation failed on vNIC transfer";
    }
}

TEST_F(NetIbMPITest, AsymmetricMerge_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    // Rank 0: create a fused vNIC (ndevs=2). Rank 1: use physical device 0 (ndevs=1).
    int vdev = -1;
    if (rank == 0) {
        ncclNetVDeviceProps_t vProps;
        vProps.ndevs = 2;
        vProps.devs[0] = 0;
        vProps.devs[1] = 1;
        ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
            << "Failed to create fused vNIC from devices 0 and 1";
        ASSERT_GE(vdev, 0);
    }

    // Inline connection setup: each rank uses a different device ID.
    // Rank 0 (receiver): listen on vdev (ndevs=2, 2 PDs, doubled QPs)
    // Rank 1 (sender):   connect on physical dev 0 (ndevs=1, 1 PD)
    // ncclIbCalculateNqps uses max(local, remote) so both sides get the same QP count.
    ConnectionPair pair;

    if (rank == 0) {
        ASSERT_EQ(CreateListenComm(vdev, &pair.handle, &pair.listenComm), ncclSuccess);
        MPI_Send(&pair.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD);

        int done = 0;
        while (!done) {
            ncclResult_t result = AcceptConnection(pair.listenComm, &pair.recvComm);
            if (result == ncclSuccess && pair.recvComm != nullptr) {
                done = 1;
            }
        }
    } else {
        MPI_Recv(&pair.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // Connect using physical device 0 (ndevs=1)
        int done = 0;
        while (!done) {
            ncclResult_t result = ConnectToRemote(0, &pair.handle, &pair.sendComm);
            if (result == ncclSuccess && pair.sendComm != nullptr) {
                done = 1;
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 510;
    const int seed = 5100;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    // Rank 0 registers on 2 PDs (vNIC), rank 1 registers on 1 PD (physical dev).
    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        memset(buffer, 0, bufferSize);
        PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        FillHostBuffer(buffer, bufferSize, seed);
        PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    int sizes[1] = {0};
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    // Verify data integrity across the asymmetric connection.
    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Received size mismatch";
        EXPECT_TRUE(VerifyHostBuffer(buffer, bufferSize, seed)) << "Data validation failed on asymmetric vNIC transfer";
    }
}

TEST_F(NetIbMPITest, CloseWithoutTransfer_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    const int rank = MPIEnvironment::world_rank;

    // Create a fused vNIC from physical devices 0 and 1.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
        << "Failed to create fused vNIC from devices 0 and 1";
    ASSERT_GE(vdev, 0);

    // Phase 1: connect through the vNIC, then close immediately without any transfer.
    // Scoped block ensures RAII teardown happens before phase 2.
    {
        ConnectionPair pair;
        NetConnectionGuard connGuard(net_);
        SetupConnectionWithGuard(vdev, pair, connGuard);
        // Guard triggers closeSend/closeRecv/closeListen on a connection that
        // never had regMr, isend, or irecv called.
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Phase 2: reconnect on the same vdev and do a transfer to verify no corruption.
    ConnectionPair pair2;
    NetConnectionGuard connGuard2(net_);
    SetupConnectionWithGuard(vdev, pair2, connGuard2);

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 520;
    const int seed = 5200;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair2.recvComm : pair2.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        memset(buffer, 0, bufferSize);
        PostSingleRecv(pair2.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        FillHostBuffer(buffer, bufferSize, seed);
        PostSendWithRetry(pair2.sendComm, buffer, bufferSize, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    int sizes[1] = {0};
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    // Verify the vNIC is still functional after the no-transfer teardown.
    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Received size mismatch";
        EXPECT_TRUE(VerifyHostBuffer(buffer, bufferSize, seed)) << "Data validation failed after no-transfer teardown reconnect";
    }
}

TEST_F(NetIbMPITest, RegDeregCycling_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    const int rank = MPIEnvironment::world_rank;

    // Create a fused vNIC from physical devices 0 and 1.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
        << "Failed to create fused vNIC from devices 0 and 1";
    ASSERT_GE(vdev, 0);

    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(vdev, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    // Cycle 50× regMr→deregMr on the same buffer to stress multi-PD MR cache.
    const int kCycles = 50;
    for (int i = 0; i < kCycles; i++) {
        void* mhandle = nullptr;
        ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess)
            << "regMr failed on cycle " << i;
        ASSERT_NE(mhandle, nullptr);
        ASSERT_EQ(DeregisterMemory(comm, mhandle), ncclSuccess)
            << "deregMr failed on cycle " << i;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Verify the connection is still functional after 50 reg/dereg cycles.
    void* mhandle = nullptr;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    const int tag = 530;
    const int seed = 5300;
    void* request = nullptr;

    // Send/recv to verify the connection works after MR cache stress.
    if (rank == 0) {
        memset(buffer, 0, bufferSize);
        PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        FillHostBuffer(buffer, bufferSize, seed);
        PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    int sizes[1] = {0};
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    // Data integrity check after MR cache cycling.
    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Received size mismatch";
        EXPECT_TRUE(VerifyHostBuffer(buffer, bufferSize, seed)) << "Data validation failed after reg/dereg cycling";
    }
}

TEST_F(NetIbMPITest, LargeTransfer_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    const int rank = MPIEnvironment::world_rank;

    // Create a fused vNIC from physical devices 0 and 1.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
        << "Failed to create fused vNIC from devices 0 and 1";
    ASSERT_GE(vdev, 0);

    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(vdev, pair, connGuard);

    // 64MB buffer — ncclIbMultiSend stripes this across the doubled QPs.
    const size_t bufferSize = 64 * 1024 * 1024;
    const int tag = 540;
    const int seed = 5400;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        memset(buffer, 0, bufferSize);
        PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        FillHostBuffer(buffer, bufferSize, seed);
        PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Extended timeout for 16MB transfer across doubled QPs.
    int sizes[1] = {0};
    ASSERT_EQ(WaitForCompletion(request, sizes, kLargeTransferTimeout), ncclSuccess);

    // Full 64MB byte-by-byte verification
    // ncclIbMultiSend would corrupt data at QP split points.
    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Large transfer size mismatch";
        EXPECT_TRUE(VerifyHostBuffer(buffer, bufferSize, seed)) << "Large vNIC transfer data validation failed";
    }
}

TEST_F(NetIbMPITest, MixedSizes_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    const int rank = MPIEnvironment::world_rank;

    // Create a fused vNIC from physical devices 0 and 1.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
        << "Failed to create fused vNIC from devices 0 and 1";
    ASSERT_GE(vdev, 0);

    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(vdev, pair, connGuard);

    // Sizes: 1B, 3MB, 3B, 5MB, 7B, 7MB, 64B, 16MB, 1B, 11MB, 4MB, 1B.
    // Tiny sizes may use only one QP, large ones stripe across both.
    // Odd MB sizes (3, 5, 7, 11) produce uneven QP splits.
    std::vector<size_t> testSizes = {
        1, 3*1024*1024, 3, 5*1024*1024, 7, 7*1024*1024,
        64, 16*1024*1024, 1, 11*1024*1024, 4*1024*1024, 1
    };

    for (size_t idx = 0; idx < testSizes.size(); idx++) {
        size_t size = testSizes[idx];
        const int tag = 550;
        const int seed = 5500 + static_cast<int>(idx);

        void* buffer = malloc(size);
        ASSERT_NE(buffer, nullptr) << "malloc failed for size " << size;
        auto bufferGuard = makeHostBufferAutoGuard(buffer);

        void* mhandle = nullptr;
        void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
        ASSERT_EQ(RegisterMemory(comm, buffer, size, NCCL_PTR_HOST, &mhandle), ncclSuccess)
            << "regMr failed for size " << size;
        NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

        void* request = nullptr;

        if (rank == 0) {
            memset(buffer, 0, size);
            PostSingleRecv(pair.recvComm, buffer, size, tag, mhandle, &request);
        } else {
            FillHostBuffer(buffer, size, seed);
            PostSendWithRetry(pair.sendComm, buffer, size, tag, mhandle, &request);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        int sizes[1] = {0};
        int timeout = (size > 1024 * 1024) ? kLargeTransferTimeout : kDefaultTimeoutMs;
        ASSERT_EQ(WaitForCompletion(request, sizes, timeout), ncclSuccess);

        // Prevent request reuse race between iterations.
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            EXPECT_EQ(sizes[0], size) << "Size mismatch for transfer of " << size << " bytes";
            EXPECT_TRUE(VerifyHostBuffer(buffer, size, seed)) << "Data validation failed for size " << size;
        }
    }
}

TEST_F(NetIbMPITest, UnalignedSizeTransfer_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    const int rank = MPIEnvironment::world_rank;

    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
        << "Failed to create fused vNIC from devices 0 and 1";
    ASSERT_GE(vdev, 0);

    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(vdev, pair, connGuard);

    // Sizes around 128-byte QP striping alignment boundaries.
    // ncclIbMultiSend computes chunkSize = DIVUP(DIVUP(size, nqps), 128) * 128.
    // These sizes produce uneven QP splits where one QP gets more data than the other.
    // 127: all on QP 0, QP 1 posts zero-sge. 129: 128B on QP 0, 1B on QP 1.
    // 255: 128B each, QP 1 gets 127B. 257: 256B on QP 0, 1B remainder on QP 1.
    std::vector<size_t> testSizes = {127, 129, 255, 257, 511, 513};

    for (size_t idx = 0; idx < testSizes.size(); idx++) {
        size_t size = testSizes[idx];
        const int tag = 560;
        const int seed = 5600 + static_cast<int>(idx);

        // Per-iteration buffer and MR registration on 2 PDs.
        void* buffer = malloc(size);
        ASSERT_NE(buffer, nullptr) << "malloc failed for size " << size;
        auto bufferGuard = makeHostBufferAutoGuard(buffer);

        void* mhandle = nullptr;
        void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
        ASSERT_EQ(RegisterMemory(comm, buffer, size, NCCL_PTR_HOST, &mhandle), ncclSuccess)
            << "regMr failed for size " << size;
        NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

        void* request = nullptr;

        if (rank == 0) {
            memset(buffer, 0, size);
            PostSingleRecv(pair.recvComm, buffer, size, tag, mhandle, &request);
        } else {
            FillHostBuffer(buffer, size, seed);
            PostSendWithRetry(pair.sendComm, buffer, size, tag, mhandle, &request);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        // Prevent request reuse race between iterations.
        MPI_Barrier(MPI_COMM_WORLD);

        // Byte level verification at the striping boundary.
        if (rank == 0) {
            EXPECT_EQ(sizes[0], size) << "Size mismatch for transfer of " << size << " bytes";
            EXPECT_TRUE(VerifyHostBuffer(buffer, size, seed)) << "Data validation failed for size " << size;
        }
    }
}

TEST_F(NetIbMPITest, Bidirectional_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    // Both ranks create a fused vNIC.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess)
        << "Failed to create fused vNIC from devices 0 and 1";
    ASSERT_GE(vdev, 0);

    // Two connections through the same vNIC with reversed roles.
    // ConnA: rank 0 receives, rank 1 sends (tag 0 for MPI handle exchange).
    // ConnB: rank 1 receives, rank 0 sends (tag 1 for MPI handle exchange).
    ConnectionPair connA, connB;

    // Phase 1: Both ranks create their listener, then exchange handles via MPI.
    // Rank 0 receives on ConnA, rank 1 receives on ConnB.
    if (rank == 0) {
        ASSERT_EQ(CreateListenComm(vdev, &connA.handle, &connA.listenComm), ncclSuccess);
    } else {
        ASSERT_EQ(CreateListenComm(vdev, &connB.handle, &connB.listenComm), ncclSuccess);
    }

    // Exchange: rank 0 sends connA handle, rank 1 sends connB handle.
    if (rank == 0) {
        MPI_Send(&connA.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD);
        MPI_Recv(&connB.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        MPI_Recv(&connA.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Send(&connB.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 1, MPI_COMM_WORLD);
    }

    // Phase 2: Connect and accept interleaved to avoid deadlock.
    // Rank 0: connect on ConnB (sender), accept on ConnA (receiver).
    // Rank 1: connect on ConnA (sender), accept on ConnB (receiver).
    ncclNetHandle_t* connectHandle = (rank == 0) ? &connB.handle : &connA.handle;
    void** connectSendComm = (rank == 0) ? &connB.sendComm : &connA.sendComm;
    void*  myListenComm    = (rank == 0) ? connA.listenComm : connB.listenComm;
    void** acceptRecvComm  = (rank == 0) ? &connA.recvComm : &connB.recvComm;

    while (*connectSendComm == nullptr || *acceptRecvComm == nullptr) {
        if (*connectSendComm == nullptr) {
            ConnectToRemote(vdev, connectHandle, connectSendComm);
        }
        if (*acceptRecvComm == nullptr) {
            AcceptConnection(myListenComm, acceptRecvComm);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // RAII guards for both connections.
    NetConnectionGuard guardA(net_);
    NetConnectionGuard guardB(net_);
    if (rank == 0) {
        guardA.setRecvComm(connA.recvComm);
        guardA.setListenComm(connA.listenComm);
        guardB.setSendComm(connB.sendComm);
    } else {
        guardA.setSendComm(connA.sendComm);
        guardB.setRecvComm(connB.recvComm);
        guardB.setListenComm(connB.listenComm);
    }

    const size_t bufferSize = kSmallBufferSize;

    // Each rank has a send buffer and a recv buffer.
    void* sendBuf = malloc(bufferSize);
    ASSERT_NE(sendBuf, nullptr);
    auto sendGuard = makeHostBufferAutoGuard(sendBuf);

    void* recvBuf = malloc(bufferSize);
    ASSERT_NE(recvBuf, nullptr);
    auto recvGuard = makeHostBufferAutoGuard(recvBuf);
    memset(recvBuf, 0, bufferSize);

    // Register send buffer on the send comm, recv buffer on the recv comm.
    void* sendComm = (rank == 0) ? connB.sendComm : connA.sendComm;
    void* recvComm = (rank == 0) ? connA.recvComm : connB.recvComm;

    void* sendMhandle = nullptr;
    ASSERT_EQ(RegisterMemory(sendComm, sendBuf, bufferSize, NCCL_PTR_HOST, &sendMhandle), ncclSuccess);
    NetMHandleGuard sendMhGuard(sendMhandle, NetMHandleDeleter(net_, sendComm));

    void* recvMhandle = nullptr;
    ASSERT_EQ(RegisterMemory(recvComm, recvBuf, bufferSize, NCCL_PTR_HOST, &recvMhandle), ncclSuccess);
    NetMHandleGuard recvMhGuard(recvMhandle, NetMHandleDeleter(net_, recvComm));

    const int sendTag = 570;
    const int recvTag = 570;
    const int sendSeed = 5700 + rank;

    // Fill send buffer with rank-specific pattern.
    FillHostBuffer(sendBuf, bufferSize, sendSeed);

    // Post recv and send simultaneously on both connections.
    void* recvRequest = nullptr;
    void* sendRequest = nullptr;

    PostSingleRecv(recvComm, recvBuf, bufferSize, recvTag, recvMhandle, &recvRequest);

    PostSendWithRetry(sendComm, sendBuf, bufferSize, sendTag, sendMhandle, &sendRequest);

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for both completions.
    int recvSizesOut[1] = {0};
    ASSERT_EQ(WaitForCompletion(recvRequest, recvSizesOut), ncclSuccess);

    int sendSizesOut[1] = {0};
    ASSERT_EQ(WaitForCompletion(sendRequest, sendSizesOut), ncclSuccess);

    MPI_Barrier(MPI_COMM_WORLD);

    // Verify received data matches the peer's send pattern.
    int peerSeed = 5700 + peerRank;
    EXPECT_EQ(recvSizesOut[0], bufferSize) << "Received size mismatch";
    EXPECT_TRUE(VerifyHostBuffer(recvBuf, bufferSize, peerSeed)) << "Bidirectional vNIC transfer data validation failed";
}

TEST_F(NetIbMPITest, FlushRepeated_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    // Check GDR support on device 0 before creating the vNIC.
    ncclNetProperties_t props;
    ASSERT_EQ(GetDeviceProperties(0, &props), ncclSuccess);
    if (!(props.ptrSupport & NCCL_PTR_CUDA)) {
        GTEST_SKIP() << "GDR not supported, skipping flush test";
    }

    // Create a fused vNIC from physical devices 0 and 1.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess);
    ASSERT_GE(vdev, 0);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(vdev, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 600;
    const int numIterations = 50;

    // Allocate GPU buffer and register with NCCL_PTR_CUDA on the vNIC connection.
    void* gpuBuffer = nullptr;
    HIP_TEST_CHECK_GTEST_FAIL(hipMalloc(&gpuBuffer, bufferSize));
    auto gpuGuard = makeDeviceBufferAutoGuard(gpuBuffer);

    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    void* mhandle = nullptr;
    ASSERT_EQ(RegisterMemory(comm, gpuBuffer, bufferSize, NCCL_PTR_CUDA, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    for (int iter = 0; iter < numIterations; iter++) {
        const int seed = 6000 + iter;
        void* request = nullptr;

        if (rank == 1) {
            // Fill GPU buffer via DeviceBufferHelpers (host vector → hipMemcpy in one call).
            ASSERT_EQ(InitializeBuffer(gpuBuffer, bufferSize, seed), hipSuccess);
            PostSendWithRetry(pair.sendComm, gpuBuffer, bufferSize, tag, mhandle, &request);
        } else {
            // Rank 0: post recv on GPU buffer.
            PostSingleRecv(pair.recvComm, gpuBuffer, bufferSize, tag, mhandle, &request);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        if (rank == 0) {
            ASSERT_EQ(sizes[0], bufferSize) << "Iter " << iter << ": received size mismatch";

            // Flush GPU memory to ensure RDMA write is visible on GPU.
            void* flushBuffers[1] = {gpuBuffer};
            int flushSizes[1] = {static_cast<int>(bufferSize)};
            void* flushHandles[1] = {mhandle};
            void* flushRequest = nullptr;

            ncclResult_t flushResult = FlushRecv(pair.recvComm, 1, flushBuffers, flushSizes,
                                                 flushHandles, &flushRequest);
            if (flushResult == ncclSuccess && flushRequest != nullptr) {
                ASSERT_EQ(WaitForCompletion(flushRequest, nullptr), ncclSuccess);
            }

            // Verify GPU buffer via DeviceBufferHelpers (hipMemcpy + compare in one call).
            ASSERT_TRUE(VerifyBuffer(gpuBuffer, bufferSize, seed))
                << "Iter " << iter << ": data verification failed after flush";
        }

        MPI_Barrier(MPI_COMM_WORLD);
    }
}

TEST_F(NetIbMPITest, SequentialTransfers_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    // Create a fused vNIC from physical devices 0 and 1.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess);
    ASSERT_GE(vdev, 0);

    const int rank = MPIEnvironment::world_rank;

    // Single connection through the vNIC, reused across all 100 iterations.
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(vdev, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 700;
    const int numIterations = 100;

    // Single buffer registered once on both sub-device PDs, reused every iteration.
    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    void* mhandle = nullptr;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    for (int iter = 0; iter < numIterations; iter++) {
        // Unique seed per iteration to detect stale data from prior rounds.
        const int seed = 7000 + iter;
        void* request = nullptr;

        if (rank == 1) {
            // Fill buffer with per-iteration pattern.
            FillHostBuffer(buffer, bufferSize, seed);
            PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
        } else {
            // Zero buffer before recv to ensure verification catches stale data.
            memset(buffer, 0, bufferSize);
            PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
        }

        // Sync both ranks before polling for completion.
        MPI_Barrier(MPI_COMM_WORLD);

        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        // Verify received data matches the iteration-specific pattern.
        if (rank == 0) {
            ASSERT_EQ(sizes[0], bufferSize) << "Iter " << iter << ": received size mismatch";
            ASSERT_TRUE(VerifyHostBuffer(buffer, bufferSize, seed)) << "Iter " << iter << ": data verification failed";
        }

        // Sync before next iteration to prevent request reuse races.
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

TEST_F(NetIbMPITest, Reconnect_VNic) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 IB devices for NIC fusion tests";
    }

    // Create a fused vNIC once, reuse across all reconnect cycles.
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ASSERT_EQ(MakeVirtualDevice(&vdev, &vProps), ncclSuccess);
    ASSERT_GE(vdev, 0);

    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 800;
    const int numCycles = 10;

    for (int cycle = 0; cycle < numCycles; cycle++) {
        const int seed = 8000 + cycle;

        // Each cycle creates a fresh connection through the same vNIC.
        ConnectionPair pair;
        ASSERT_EQ(SetupConnection(vdev, pair, rank, peerRank), ncclSuccess)
            << "Cycle " << cycle << ": SetupConnection failed";

        // Allocate and register a fresh buffer per cycle.
        void* buffer = malloc(bufferSize);
        ASSERT_NE(buffer, nullptr);

        void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
        void* mhandle = nullptr;
        ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess)
            << "Cycle " << cycle << ": regMr failed";

        void* request = nullptr;

        if (rank == 1) {
            // Fill with cycle-specific pattern.
            FillHostBuffer(buffer, bufferSize, seed);
            PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
        } else {
            memset(buffer, 0, bufferSize);
            PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        // Verify received data matches the cycle-specific pattern.
        if (rank == 0) {
            ASSERT_EQ(sizes[0], bufferSize) << "Cycle " << cycle << ": received size mismatch";
            ASSERT_TRUE(VerifyHostBuffer(buffer, bufferSize, seed)) << "Cycle " << cycle << ": data verification failed";
        }

        MPI_Barrier(MPI_COMM_WORLD);

        // Manually deregister and close all resources for this cycle.
        // deregMr iterates ndevs=2, removing MR cache entries for each sub-device.
        ASSERT_EQ(DeregisterMemory(comm, mhandle), ncclSuccess)
            << "Cycle " << cycle << ": deregMr failed";

        // closeSend/closeRecv destroy per-sub-device QPs, PDs, FIFO MRs, and sockets.
        if (rank == 0) {
            ASSERT_EQ(CloseRecvComm(pair.recvComm), ncclSuccess);
            ASSERT_EQ(CloseListenComm(pair.listenComm), ncclSuccess);
        } else {
            ASSERT_EQ(CloseSendComm(pair.sendComm), ncclSuccess);
        }

        free(buffer);

        // Sync before next cycle to ensure both ranks have fully torn down.
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

#endif // MPI_TESTS_ENABLED
