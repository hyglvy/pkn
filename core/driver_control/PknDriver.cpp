#include <Windows.h>
#include <winternl.h>
#include <ntddmou.h>

#include <string>
#include <string_view>
#include <xutility>

#include "PknDriver.h"

#include "../../../kernel/io_code.h"
#include "../../../kernel/names.h"

#define EncryptInputByXor() xor_memory((char *)&inp + 8, sizeof(inp) - 8, xor_key); inp.xor_val = this->xor_key
#define DecryptOutputByXor() xor_memory((char *)&oup, sizeof(oup), xor_key)

namespace pkn
{
PknDriver::PknDriver()
{}

void PknDriver::xor_memory(void *address, size_t size, uint64_t xor_key) noexcept
{
    for (size_t i = 0; i + 8 <= size; i += 8)
    {
        *(uint64_t *)((char *)address + i) ^= xor_key;
    }
}

bool PknDriver::query_system_information(
    uint64_t informaiton_class,
    void *buffer,
    uint32_t buffer_size,
    size_t *ret_size
) const noexcept
{
    QuerySystemInformationInput inp;
    inp.xor_val = xor_key;
    inp.id = 0;
    inp.information_class = informaiton_class;
    inp.buffer_size = buffer_size;
    inp.size_only = buffer == nullptr;

    EncryptInputByXor();
    if (buffer == nullptr)
    {
        // query size
        uint32_t size_buffer = 0;
        uint32_t out_size = sizeof(size_buffer);
        if (ioctl(IOCTL_PLAYERKNOWNS_QUERY_SYSTEM_INFORMATION, &inp, sizeof(inp), &size_buffer, &out_size))
        {
            *ret_size = size_buffer;
            return true;
        }
        return false;
    }
    else
    {
        // query information
        uint32_t out_size = (uint32_t)buffer_size;
        if (ioctl(IOCTL_PLAYERKNOWNS_QUERY_SYSTEM_INFORMATION, &inp, sizeof(inp), buffer, &out_size))
        {
            xor_memory(buffer, out_size, xor_key);
            *ret_size = out_size;
            return true;
        }
        return false;
    }
}

bool PknDriver::read_process_memory(const pid_t &pid, const erptr_t &remote_address, size_t size, void *buffer) const noexcept
{
    ReadProcessMemoryInput inp = { xor_key, pid, remote_address, size, (uint64_t)buffer };
    //inp.xor_val = 0;
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_READ_PROCESS_MEMORY, &inp, sizeof(inp), nullptr, nullptr))
    {
        xor_memory(buffer, size, xor_key);
        return true;
    }
    return false;
}

bool PknDriver::write_process_memory(pid_t pid, erptr_t remote_address, size_t size, const void *data) const noexcept
{
	WriteProcessMemoryInput inp;
	inp.xor_val = xor_key;
	inp.processid = pid;
	inp.startaddress = remote_address;
	inp.bytestowrite = size;
	inp.buffer = (uint64_t)data;
    EncryptInputByXor();
    // !!!!!!note: buffer for write process memory is not xor protected!!!!!
    return ioctl(IOCTL_PLAYERKNOWNS_WRITE_PROCESS_MEMORY, &inp, sizeof(inp), nullptr, nullptr);
}

bool PknDriver::acquire_lock(const pid_t &pid, const erptr_t &remote_lock_address) const noexcept
{
    AcquireLockInput inp;
    AcquireLockOutput oup;
    uint32_t out_size = sizeof(oup);
    inp.xor_val = xor_key;
    inp.processid = pid;
    inp.remote_lock_address = remote_lock_address;
    //inp.xor_val = 0;
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_ACQUIRE_LOCK, &inp, sizeof(inp), &oup, &out_size))
    {
        return oup.succeed;
    }
    return false;
}

std::optional<uint64_t> PknDriver::read_system_memory(erptr_t remote_address, size_t size, void *buffer) const noexcept
{
    ReadSystemMemoryInput inp;
    ReadSystemMemoryOutput oup;
    uint32_t out_size = sizeof(oup);
    inp.xor_val = xor_key;
    inp.addresstoread = remote_address;
    inp.bytestoread = size;
    inp.buffer = (uint64_t)buffer;
    //inp.xor_val = 0;
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_READ_SYSTEM_MEMORY, &inp, sizeof(inp), &oup, &out_size))
    {
        xor_memory(buffer, size, xor_key);
        DecryptOutputByXor();
        return oup.bytesread;
    }
    return std::nullopt;
}

std::optional<uint64_t> PknDriver::write_system_memory(erptr_t remote_address, size_t size, const void *data) const noexcept
{
    WriteSystemMemoryInput inp;
    WriteSystemMemoryOutput oup;
    uint32_t out_size = sizeof(oup);
    inp.xor_val = xor_key;
    inp.addresstowrite = remote_address;
    inp.bytestowrite = size;
    inp.buffer = (uint64_t)data;
    EncryptInputByXor();
    // !!!!!!note: buffer for write process memory is not xor protected!!!!!
    if (ioctl(IOCTL_PLAYERKNOWNS_WRITE_SYSTEM_MEMORY, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        return oup.byteswritten;
    }
    return std::nullopt;
}

std::optional<erptr_t> PknDriver::allocate_nonpaged_memory(size_t size) const noexcept
{
    AllocateNonpagedMemoryInput inp;
    inp.xor_val = xor_key;
    inp.size = size;

    AllocateNonpagedMemoryOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_ALLOCATE_NONPAGED_MEMORY, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        return erptr_t(oup.address);
    }
    return std::nullopt;
}

bool PknDriver::free_nonpaged_memory(erptr_t ptr) const noexcept
{
    return false;
}

bool PknDriver::force_write_process_memory(pid_t pid, const erptr_t &remote_address, size_t size, const void *data) const noexcept
{
    const int once_write = 0x1000;
    if (size <= once_write)
    {
        WriteProcessMemoryInput inp = { xor_key, pid, remote_address, size, (uint64_t)data };
        EncryptInputByXor();
        return ioctl(IOCTL_PLAYERKNOWNS_FORCE_WRITE_PROCESS_MEMORY, &inp, sizeof(inp), nullptr, nullptr);
    }
    else
    {
        rptr_t r = remote_address;
        char *pd = (char*)data;
        size_t size_left = size;
        while (size_left > 0)
        {
            if (!force_write_process_memory(pid, r, size_left < once_write ? size_left : once_write, pd))
                break;
            r += once_write;
            pd += once_write;
            size_left -= once_write;
        }
        return size_left != size;
    }
}

bool PknDriver::virtual_query(pid_t pid, erptr_t remote_address, MEMORY_BASIC_INFORMATION *mbi) const noexcept
{
    QueryVirtualMemoryInput inp = { xor_key, pid, remote_address };
    QueryVirtualMemoryOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_QUERY_VIRTUAL_MEMORY, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        *mbi = oup.mbi;
        return true;
    }
    return false;
}

bool PknDriver::get_mapped_file(pid_t pid, uint64_t address, estr_t *mapped_file) const noexcept
{
    GetMappedFileInput inp = { xor_key, pid, address };
    GetMappedFileOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_GET_MAPPED_FILE, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        estr_t retv;
        retv.reserve(512);
        auto i = std::back_inserter(retv);
        for (auto p = oup.image_path; *p; p++)
        {
            *i++ = *p;
        }
        *mapped_file = retv;
        return true;
    }
    return false;
}

bool PknDriver::get_process_base(pid_t pid, erptr_t *base) const noexcept
{
    GetProcessBaseInput inp = { xor_key, pid };

    GetProcessBaseOutput oup;
    uint32_t out_size = sizeof(oup);

    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_GET_PROCESS_BASE, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        *base = oup.base;
        return true;
    }
    return false;
}

bool PknDriver::get_process_times(pid_t pid, uint64_t * pcreation_time, uint64_t * pexit_time, uint64_t * pkernel_time, uint64_t * puser_time) const noexcept
{
    GetProcessTimesInput inp = { xor_key, pid };
    GetProcessTimesOutput oup;
    uint32_t out_size = sizeof(oup);

    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_GET_PROCESS_TIMES, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        if (pcreation_time) *pcreation_time = oup.creation_time;
        if (pexit_time) *pexit_time = oup.exit_time;
        if (pkernel_time) *pkernel_time = oup.kernel_time;
        if (puser_time) *puser_time = oup.user_time;
        return true;
    }
    return false;
}

std::optional<estr_t> PknDriver::get_process_name(pid_t pid) const noexcept
{
    GetProcessNameInput inp = { xor_key, pid };
    GetProcessNameOutput oup;
    uint32_t out_size = sizeof(oup);

    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_GET_PROCESS_NAME, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        estr_t retv;
        retv.reserve(512);
        auto i = std::back_inserter(retv);
        for (auto p = oup.process_name; *p; p++)
        {
            *i++ = *p;
        }
        return retv;
    }
    return std::nullopt;
}

erptr_t PknDriver::get_peb_address() const noexcept
{
    // #todo_get_peb_address
    return 0;
}

bool PknDriver::get_process_exit_status(pid_t pid, NTSTATUS *status)const noexcept
{
    TestProcessInput inp = { xor_key, pid };

    TestProcessOutput oup;
    uint32_t out_size = sizeof(oup);

    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_GET_PROCESS_EXIT_STATUS, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        if (status) *status = oup.status;
        return true;
    }
    return false;
}

bool PknDriver::wait_for_process(pid_t pid, uint64_t timeout_nanosec, NTSTATUS *status) const noexcept
{
    WaitProcessInput inp = { xor_key, pid, timeout_nanosec };

    WaitProcessOutput oup;
    uint32_t out_size = sizeof(oup);

    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_WAIT_FOR_PROCESS, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        if (status) *status = oup.status;
        return true;
    }
    return false;
}

bool PknDriver::wait_for_thread(pid_t tid, uint64_t timeout_nanosec, NTSTATUS *status) const noexcept
{
    WaitThreadInput inp = { xor_key, tid, timeout_nanosec };

    WaitThreadOutput oup;
    uint32_t out_size = sizeof(oup);

    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_WAIT_FOR_THREAD, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        if (status) *status = oup.status;
        return true;
    }
    return false;
}

bool PknDriver::_get_physical_memory_address(pid_t pid, erptr_t remote_address, uint64_t *pphysical_address) const noexcept
{
    return false;
    //throw kernel_not_implemented_error();
    //uint32_t outputSize = sizeof(*pphysical_address);
    //return ioctl(IOCTL_PLAYERKNOWNS_GET_PHISICAL_ADDRESS, &in, sizeof(in), pphysical_address, &outputSize);
}

bool PknDriver::_write_physical_memory(erptr_t remote_address, size_t size, const void *data) const noexcept
{
    return false;
    //throw kernel_not_implemented_error();
    //struct input
    //{
    //    uint64_t startaddress;
    //    uint64_t bytestowrite;
    //};
    //auto buffer_size = size + sizeof(struct input);

    //char *buffer = new char[buffer_size];
    //auto in = (struct input *)buffer;
    //in->startaddress = remote_address;
    //in->bytestowrite = size;
    //memcpy((void *)((uintptr_t)in + sizeof(struct input)), data, size);

    //return ioctl(IOCTL_PLAYERKNOWNS_WRITE_PHISICAL_MEMORY, buffer, (decltype(in->bytestowrite))buffer_size);
}

bool PknDriver::query_thread_information(uint64_t tid, uint64_t informaiton_class, void *buffer, uint32_t buffer_size, size_t *ret_size) const noexcept
{
    QueryThreadInformationInput inp = { xor_key,
        tid,
        informaiton_class,
        buffer_size,
    };
    EncryptInputByXor();
    if (buffer == nullptr)
    {
        // query size
        uint32_t size_buffer = 0;
        uint32_t out_size = sizeof(size_buffer);
        if (ioctl(IOCTL_PLAYERKNOWNS_QUERY_THREAD_INFORMATION, &inp, sizeof(inp), &size_buffer, &out_size))
        {
            *ret_size = size_buffer;
            return true;
        }
        return false;
    }
    else
    {
        uint32_t out_size = (uint32_t)buffer_size;
        if (ioctl(IOCTL_PLAYERKNOWNS_QUERY_THREAD_INFORMATION, &inp, sizeof(inp), buffer, &out_size))
        {
            xor_memory(buffer, out_size, xor_key);
            *ret_size = out_size;
            return true;
        }
        return false;
    }
}

std::optional<erptr_t> PknDriver::get_teb_address(uint64_t tid)
{
    typedef struct _THREAD_BASIC_INFORMATION
    {
        NTSTATUS                ExitStatus;
        PVOID                   TebBaseAddress;
        CLIENT_ID               ClientId;
        KAFFINITY               AffinityMask;
        KPRIORITY               Priority;
        KPRIORITY               BasePriority;
    } THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

    size_t ret_size;
    THREAD_BASIC_INFORMATION tbi;
    if (query_thread_information(tid, 0x00 /*ThreadBasicInformation = 0x00*/, &tbi, sizeof(THREAD_BASIC_INFORMATION), &ret_size))
    {
        return erptr_t((uint64_t)tbi.TebBaseAddress);
    }
    return std::nullopt;
}

bool PknDriver::create_user_thread(pid_t pid,
                                   _In_opt_ PSECURITY_DESCRIPTOR ThreadSecurityDescriptor,
                                   _In_ bool CreateSuspended,
                                   _In_opt_ uint64_t MaximumStackSize,
                                   _In_ uint64_t CommittedStackSize,
                                   _In_ uint64_t StartAddress,
                                   _In_opt_ uint64_t Parameter,
                                   _Out_opt_ pid_t *out_pid,
                                   _Out_opt_ pid_t *tid
) const noexcept
{
    CreateUserThreadInput inp = { xor_key,
        pid,
        {},
        ThreadSecurityDescriptor == nullptr ? false : true,
        CreateSuspended,
        MaximumStackSize,
        CommittedStackSize,
        StartAddress,
        Parameter
    };
    if (ThreadSecurityDescriptor != nullptr)
    {
        memcpy(&inp.sd, ThreadSecurityDescriptor, sizeof(SECURITY_DESCRIPTOR));
    }

    CreateUserThreadOutput oup;
    uint32_t out_size = sizeof(oup);

    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_CREATE_USER_THREAD, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        *out_pid = oup.pid;
        *tid = oup.tid;
        return true;
    }
    return false;
}

bool PknDriver::allocate_virtual_memory(pid_t pid, erptr_t address, size_t size, uint32_t type, uint32_t protect, erptr_t *allocated_base, size_t *allocated_size) const noexcept
{
    AllocateVirtualMemoryInput inp = { xor_key,
        pid,
        (UINT64)address,
        size,
        type,
        protect
    };

    AllocateVirtualMemoryOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_ALLOCATE_VIRTUAL_MEMORY, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        if (allocated_base) *allocated_base = oup.address;
        if (allocated_size) *allocated_size = oup.size;
        return true;
    }
    return false;
}


bool PknDriver::free_virtual_memory(pid_t pid, erptr_t address, size_t size, uint32_t type, erptr_t *freed_base /*= nullptr*/, size_t *freed_size /*= nullptr*/) const noexcept
{
    FreeVirtualMemoryInput inp = { xor_key,
        pid,
        (UINT64)address,
        size,
        type
    };
    FreeVirtualMemoryOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_FREE_VIRTUAL_MEMORY, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        if (freed_base) *freed_base = oup.address;
        if (freed_size) *freed_size = oup.size;
        return true;
    }
    return false;
}

bool PknDriver::protect_virtual_memory(pid_t pid, erptr_t address, size_t size, uint32_t protect, erptr_t *protected_base /*= nullptr*/, size_t *protected_size /*= nullptr*/, uint32_t *old_protect /*= nullptr*/) const noexcept
{
    ProtectVirtualMemoryInput inp = { xor_key,
        pid,
        (UINT64)address,
        size,
        protect
    };
    ProtectVirtualMemoryOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_PROTECT_VIRTUAL_MEMORY, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        if (protected_base) *protected_base = oup.address;
        if (protected_size) *protected_size = oup.size;
        if (old_protect) *old_protect = oup.old_protect;
        return true;
    }
    return false;
}

std::optional<uint64_t> PknDriver::delete_unloaded_drivers(erptr_t rva_mm_unloaded_drivers, erptr_t rva_mm_last_unloaded_driver, estr_t name_pattern) const noexcept
{
    DeleteUnloadedDriversInput inp;
    inp.xor_val = xor_key;
    inp.pMmUnloadedDrivers = (UINT64)rva_mm_unloaded_drivers;
    inp.pMmLastUnloadedDriver = (UINT64)rva_mm_last_unloaded_driver;
    if (name_pattern.size() >= sizeof(inp.name) / sizeof(*inp.name))
        return std::nullopt;
    //if (name_pattern.size() > sizeof(inp.name))
    //    name_pattern.resize(sizeof(inp.name) - sizeof(*inp.name));
    memcpy(&inp.name, name_pattern.to_wstring().c_str(), name_pattern.size() * sizeof(name_pattern[0]));
    inp.name[name_pattern.size()] = 0;

    DeleteUnloadedDriversOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_DELETE_UNLOADED_DRIVERS, &inp, sizeof(inp), &oup, &out_size))
    {
        return oup.ndelete;
    }
    return std::nullopt;
}

std::optional<uint64_t> PknDriver::run_driver_entry(erptr_t entry, uint64_t arg1, uint64_t arg2) const noexcept
{
    RunDriverEntryInput inp;
    inp.xor_val = xor_key;
    inp.start_address = entry;
    inp.parameter1 = arg1;
    inp.parameter2 = arg2;

    RunDriverEntryOutput oup;
    uint32_t out_size = sizeof(oup);
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_RUN_DRIVER_ENTRY, &inp, sizeof(inp), &oup, &out_size))
    {
        DecryptOutputByXor();
        return oup.ret_val;
    }
    return false;
}

//std::optional<uint64_t> PknDriver::map_and_run_driver(erptr_t image_buffer,
//                                   euint64_t image_size,
//                                   erptr_t entry_rva,
//                                   erptr_t parameter1,
//                                   erptr_t parameter2,
//                                   erptr_t parameter1_is_rva,
//                                   erptr_t parameter2_is_rva)
//{
//    MapAndRunDriverInput inp;
//    inp.xor_val = xor_key;
//    inp.image_buffer = image_buffer;
//    inp.image_size = image_size;
//    inp.entry_rva = entry_rva;
//    inp.parameter1 = parameter1;
//    inp.parameter2 = parameter2;
//    inp.parameter1_is_rva = parameter1_is_rva;
//    inp.parameter2_is_rva = parameter2_is_rva;
//
//    MapAndRunDriverOutput oup;
//    uint32_t out_size = sizeof(oup);
//    EncryptInputByXor();
//    if (ioctl(IOCTL_PLAYERKNOWNS_MAP_AND_RUN_DRIVER, &inp, sizeof(inp), &oup, &out_size))
//    {
//        return oup.ret_val;
//    }
//    return std::nullopt;
//}

bool PknDriver::get_mouse_pos(int *x, int *y) const noexcept
{
    GetMousePosOutput oup;
    uint32_t out_size = sizeof(oup);

    if (ioctl(IOCTL_PLAYERKNOWNS_GET_MOUSE_POS, nullptr, 0, &oup, &out_size))
    {
        *x = oup.x;
        *y = oup.y;
        return true;
    }
    return false;
}

bool PknDriver::protect_process(pid_t pid) const noexcept
{
    ProtectProcessInput inp{ xor_key, pid };
    EncryptInputByXor();
    if (ioctl(IOCTL_PLAYERKNOWNS_PROTECT_PROCESS, &inp, sizeof(inp), nullptr, nullptr))
        return true;
    return false;
}

bool PknDriver::unprotect_process() const noexcept
{
    return ioctl(IOCTL_PLAYERKNOWNS_UNPROTECT_PROCESS, nullptr, 0, nullptr, nullptr);
}

//
//void PknDriver::set_mouse_pos(int x, int y)
//{
//    SynthesizeMouseData data = { 0 };
//    data.LastX = x;
//    data.LastY = y;
//    data.Flags = 0;
//    return synthesize_mouse(&data, 1);
//}
//
//void PknDriver::synthesize_mouse(SynthesizeMouseData *datas, size_t count)
//{
//    static_assert(sizeof(SynthesizeMouseData) == sizeof(SynthesizeMouseInputData));
//
//    uint8_t buffer[0x1000];
//    bool need_free = false;
//    const size_t data_size = sizeof(SynthesizeMouseData) * count;
//    const size_t input_size = sizeof(SynthesizeMouseInputHead) + data_size;
//    SynthesizeMouseInputHead *pinput;
//    if (input_size <= sizeof(buffer))
//    {
//        pinput = (SynthesizeMouseInputHead *)buffer;
//    }
//    else
//    {
//        pinput = (SynthesizeMouseInputHead *)new char[input_size];
//        need_free = true;
//    }
//    pinput->count = count;
//    pinput->fill = 0;
//    memcpy(pinput + 1, datas, data_size);
//
//    if (ioctl(IOCTL_PLAYERKNOWNS_SYNTHESIZE_MOUSE, pinput, (uint32_t)input_size, nullptr, nullptr))
//    {
//        if (need_free)
//            delete pinput;
//        return;
//    }
//
//    if (need_free)
//        delete pinput;
//    throw kernel_synthesize_mouse_error();
//}
}
