/*************************************************************************
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "NetIbMPITestBase.hpp"

#ifdef MPI_TESTS_ENABLED

// Initialization Tests

TEST_F(NetIbMPITest, InitializePlugin) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ncclResult_t result = InitNetIb();
    ASSERT_EQ(result, ncclSuccess) << "Failed to initialize NET IB plugin";
    EXPECT_NE(initCtx_, nullptr) << "Init context should be set on success";
}

TEST_F(NetIbMPITest, GetDeviceCount) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    EXPECT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    EXPECT_GT(ndev, 0) << "No IB devices found";

    if (MPIEnvironment::world_rank == 0) {
        TEST_INFO("Found %d IB device(s)", ndev);
    }
}

// Device Properties Tests

TEST_F(NetIbMPITest, GetDeviceProperties) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    for (int i = 0; i < ndev; i++) {
        ncclNetProperties_t props;
        memset(&props, 0, sizeof(props));

        EXPECT_EQ(GetDeviceProperties(i, &props), ncclSuccess)
            << "Failed to get properties for device " << i;

        EXPECT_NE(props.name, nullptr) << "Device " << i << " has NULL name";
        EXPECT_GT(props.speed, 0) << "Device " << i << " has invalid speed";
        EXPECT_NE(props.pciPath, nullptr) << "Device " << i << " has NULL pciPath";

        if (MPIEnvironment::world_rank == 0) {
            TEST_INFO("Device %d: name=%s speed=%d pciPath=%s",
                   i, props.name, props.speed, props.pciPath);
        }
    }
}

TEST_F(NetIbMPITest, GetDevicePropertiesInvalidDevice) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    ncclNetProperties_t props;

    // Invalid device ID (too large)
    ncclResult_t result = GetDeviceProperties(ndev + kInvalidDeviceOffset, &props);
    EXPECT_NE(result, ncclSuccess) << "Should fail for invalid device ID";
}

// Connection Setup Tests

TEST_F(NetIbMPITest, ListenAndConnect) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    if (rank == 0) {
        EXPECT_NE(pair.recvComm, nullptr) << "Recv comm should be established";
        EXPECT_NE(pair.listenComm, nullptr) << "Listen comm should exist";
    } else {
        EXPECT_NE(pair.sendComm, nullptr) << "Send comm should be established";
    }
}

TEST_F(NetIbMPITest, ConnectWithInvalidHandle) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    ncclNetHandle_t invalidHandle;
    memset(&invalidHandle, 0xFF, sizeof(invalidHandle));
    void* sendComm = nullptr;

    // Negative test: Connect with garbage handle
    ncclResult_t result = ConnectToRemote(0, &invalidHandle, &sendComm);
    EXPECT_EQ(result, ncclInternalError) << "Should fail with invalid handle";
}

// Memory Registration Tests

TEST_F(NetIbMPITest, RegisterHostMemory) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    EXPECT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    EXPECT_NE(mhandle, nullptr);

    // Use NetMHandleGuard for automatic deregistration before connection closes
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));
}

TEST_F(NetIbMPITest, RegisterGpuMemory) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    void* buffer = nullptr;
    HIP_TEST_CHECK_GTEST_FAIL(hipMalloc(&buffer, bufferSize));
    auto bufferGuard = makeDeviceBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    EXPECT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_CUDA, &mhandle), ncclSuccess);
    EXPECT_NE(mhandle, nullptr);

    // Use NetMHandleGuard for automatic deregistration before connection closes
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));
}

TEST_F(NetIbMPITest, RegisterMemoryNullPointer) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    // Negative test: NULL buffer pointer
    ncclResult_t result = RegisterMemory(comm, nullptr, 4096, NCCL_PTR_HOST, &mhandle);
    EXPECT_NE(result, ncclSuccess) << "Should fail with NULL buffer";
}

TEST_F(NetIbMPITest, DeregisterNullHandle) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    // Edge case: Deregister NULL handle (should be no-op)
    EXPECT_EQ(DeregisterMemory(comm, nullptr), ncclSuccess);
}

// Send/Recv Tests

TEST_F(NetIbMPITest, SimpleSendRecv) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 42;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);

    // Use NetMHandleGuard for automatic cleanup on failure (exception safety)
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        // Receiver
        PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        // Sender
        FillHostBuffer(buffer, bufferSize, rank);
        PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for completion
    int sizes[1] = {0};
    ASSERT_NE(request, nullptr) << "Request must be non-NULL before waiting";
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Received size mismatch";

        // Verify received data
        int senderRank = 1;  // Data was sent by rank 1
        EXPECT_TRUE(VerifyHostBuffer(buffer, bufferSize, senderRank)) << "Data validation failed";
    }

    // NetMHandleGuard will automatically deregister memory when test scope ends
    // Destructor order ensures MR is deregistered before connection closes:
    //   1. mhandleGuard destructor (deregisters MR)
    //   2. bufferGuard destructor (frees buffer)
    //   3. connGuard destructor (closes connection)
}

TEST_F(NetIbMPITest, SendRecvMultipleSizes) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    // Test various sizes
    std::vector<size_t> testSizes = {1, 64, 256, 1024, 4096, 16384, 65536};

    for (size_t size : testSizes) {
        const int tag = 100;
        const int seed = 2000 + static_cast<int>(size);  // Unique seed per size

        void* buffer = malloc(size);
        ASSERT_NE(buffer, nullptr);
        auto bufferGuard = makeHostBufferAutoGuard(buffer);  // Local guard for loop iteration

        void* mhandle = nullptr;
        void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
        ASSERT_EQ(RegisterMemory(comm, buffer, size, NCCL_PTR_HOST, &mhandle), ncclSuccess);
        NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

        void* request = nullptr;

        if (rank == 0) {
            memset(buffer, 0, size);
            PostSingleRecv(pair.recvComm, buffer, size, tag, mhandle, &request);
            ASSERT_NE(request, nullptr) << "Recv request should never be NULL";
        } else {
            FillHostBuffer(buffer, size, seed);
            PostSendWithRetry(pair.sendComm, buffer, size, tag, mhandle, &request);
        }

        // Barrier 1: Ensure both ranks have posted their operations before waiting
        MPI_Barrier(MPI_COMM_WORLD);

        // Wait for completion
        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        // Barrier 2: CRITICAL - Ensure BOTH ranks have completed before EITHER continues
        // This prevents rank A from starting next transfer while rank B is still
        // completing current transfer, which would cause request object reuse race conditions
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            EXPECT_EQ(sizes[0], size) << "Size mismatch for transfer of " << size << " bytes";
            EXPECT_TRUE(VerifyHostBuffer(buffer, size, seed)) << "Data validation failed for size " << size;
        }

        // NetMHandleGuard will automatically deregister at end of loop iteration
    }
}

TEST_F(NetIbMPITest, SendRecvZeroSize) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 50;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        // Receiver - expect zero bytes
        PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        // Sender - send zero bytes
        PostSendWithRetry(pair.sendComm, buffer, 0, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for completion
    int sizes[1] = {-1};
    ASSERT_NE(request, nullptr) << "Request must be non-NULL before waiting";
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    if (rank == 0) {
        EXPECT_EQ(sizes[0], 0) << "Should receive zero bytes";
    }
}

// Stress and Edge Case Tests

// Tests multiple sequential transfers on the same connection using host memory.
// This test validates that request objects can be properly reused across multiple
// send/recv operations without resource exhaustion or state corruption.
//
// SYNCHRONIZATION STRATEGY:
//   Two barriers per iteration ensure proper ordering:
//   1. After Post: Ensures both ranks have posted before either waits
//   2. After Completion: CRITICAL - Ensures BOTH ranks complete before EITHER
//      starts next iteration. Without this, rapid request reuse causes races.
//
// NULL REQUEST HANDLING:
//   NET IB isend() can return ncclSuccess with NULL request when the FIFO
//   isn't ready yet (receiver's irecv RDMA write hasn't reached sender yet).
//   This is NOT an error - it means "try again". The sender must retry until
//   it gets a valid request pointer. This is normal NET IB protocol behavior.
//
// NOTE: Flush (iflush) is intentionally NOT called because:
//   1. Flush is only needed for GPU Direct RDMA to ensure data visibility
//   2. For NCCL_PTR_HOST transfers, flush is unnecessary
TEST_F(NetIbMPITest, MultipleSequentialTransfers) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    const size_t bufferSize = kSmallBufferSize;
    const int numTransfers = kNumSequentialTransfers;

    void* sendBuffer = nullptr;
    void* recvBuffer = nullptr;
    HostBufferAutoGuard sendBufferGuard(nullptr);
    HostBufferAutoGuard recvBufferGuard(nullptr);

    if (rank == 0) {
        recvBuffer = malloc(bufferSize);
        ASSERT_NE(recvBuffer, nullptr);
        recvBufferGuard = makeHostBufferAutoGuard(recvBuffer);
    } else {
        sendBuffer = malloc(bufferSize);
        ASSERT_NE(sendBuffer, nullptr);
        sendBufferGuard = makeHostBufferAutoGuard(sendBuffer);
    }

    void* mhandle = nullptr;
    void* buffer = (rank == 0) ? recvBuffer : sendBuffer;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    for (int i = 0; i < numTransfers; i++) {
        const int tag = kTransferTagBase + i;
        const int seed = kBaseSeedOffset + i;  // Unique seed for each transfer
        void* request = nullptr;

        if (rank == 0) {
            memset(recvBuffer, 0, bufferSize);
            PostSingleRecv(pair.recvComm, recvBuffer, bufferSize, tag, mhandle, &request);
            ASSERT_NE(request, nullptr) << "Recv request should never be NULL";
        } else {
            FillHostBuffer(sendBuffer, bufferSize, seed);
            PostSendWithRetry(pair.sendComm, sendBuffer, bufferSize, tag, mhandle, &request);
        }

        // Barrier 1: Ensure both ranks have posted their operations before waiting
        MPI_Barrier(MPI_COMM_WORLD);

        // Wait for completion
        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        // Barrier 2: CRITICAL - Ensure BOTH ranks have completed before EITHER continues
        // This prevents rank A from starting transfer N+1 while rank B is still
        // completing transfer N, which would cause request object reuse race conditions
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            EXPECT_EQ(sizes[0], bufferSize) << "Transfer " << i << " size mismatch";

            EXPECT_TRUE(VerifyHostBuffer(recvBuffer, bufferSize, seed)) << "Transfer " << i << " data validation failed (seed=" << seed << ")";

            // NOTE: Flush is NOT called for host memory transfers
            // Flush (iflush) is only needed for GPU Direct RDMA to ensure data visibility on GPU.
            // For NCCL_PTR_HOST transfers, flush is unnecessary and calling it can cause
            // race conditions when request objects are rapidly reused.
            // The NET IB implementation will no-op the flush call for host memory anyway.
        }
    }

    // NetMHandleGuard will automatically deregister at scope end
}

TEST_F(NetIbMPITest, LargeTransfer) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    const int rank = MPIEnvironment::world_rank;
    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);

    const size_t bufferSize = kLargeBufferSize; // 16 MB
    const int tag = 400;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        // Receiver
        PostSingleRecv(pair.recvComm, buffer, bufferSize, tag, mhandle, &request);
    } else {
        // Sender
        FillHostBuffer(buffer, bufferSize, rank);
        PostSendWithRetry(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for completion with longer timeout for large transfer
    int sizes[1] = {0};
    ASSERT_NE(request, nullptr) << "Request must be non-NULL before waiting";
    ASSERT_EQ(WaitForCompletion(request, sizes, kLargeTransferTimeout), ncclSuccess);

    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Large transfer size mismatch";

        // Verify received data
        int senderRank = 1;  // Data was sent by rank 1
        EXPECT_TRUE(VerifyHostBuffer(buffer, bufferSize, senderRank)) << "Large transfer data validation failed";
    }

    // NetMHandleGuard will automatically deregister at scope end
}

TEST_F(NetIbMPITest, CloseWithoutWaitingForCompletion) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int ndev = 0;
    AssertInitAndGetDevices(&ndev);

    ConnectionPair pair;
    NetConnectionGuard connGuard(net_);
    SetupConnectionWithGuard(0, pair, connGuard);
}

#endif // MPI_TESTS_ENABLED
