#pragma once

#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_conf.hpp>
#include <unistd.h>

namespace naoto
{
    // Engine calls this. Wipes any stale segment and makes a fresh one.
    inline OrderQueue *CreateSharedQueue(void)
    {
        shm_unlink(SharedMemoryPath);

        int fd = shm_open(SharedMemoryPath, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd == -1)
        {
            std::cout << "1 Shared memory failed.\n\n";
            return nullptr;
        }
        if (ftruncate(fd, sizeof(OrderQueue)) == -1)
        {
            close(fd);
            shm_unlink(SharedMemoryPath);
            std::cout << "2 Shared memory failed.\n\n";
            return nullptr;
        }

        void *ptr = mmap(nullptr, sizeof(OrderQueue), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);

        struct stat st{};
        if (fstat(fd, &st) == 0)
        {
            std::cout << "shm inode=" << st.st_ino << " size=" << st.st_size
                      << " expected=" << sizeof(OrderQueue) << '\n';
        }

        close(fd);

        if (ptr == MAP_FAILED)
        {
            shm_unlink(SharedMemoryPath);
            std::cout << "3 Shared memory failed.\n\n";
            return nullptr;
        }

        return new (ptr) OrderQueue{};
    }

    // Gateway calls this. Returns nullptr if the engine isn't up yet.
    inline OrderQueue *OpenSharedQueue(void)
    {
        int fd = shm_open(SharedMemoryPath, O_RDWR, 0666);
        if (fd == -1)
        {
            std::cout << "4 Shared memory failed.\n\n";
            return nullptr;
        }

        void *ptr = mmap(nullptr, sizeof(OrderQueue), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);

        struct stat st{};
        if (fstat(fd, &st) == 0)
        {
            std::cout << "shm inode=" << st.st_ino << " size=" << st.st_size
                      << " expected=" << sizeof(OrderQueue) << '\n';
        }

        close(fd);

        if (ptr == MAP_FAILED)
        {
            std::cout << "5 Shared memory failed.\n\n";
            return nullptr;
        }

        return static_cast<OrderQueue *>(ptr);
    }
} // namespace naoto