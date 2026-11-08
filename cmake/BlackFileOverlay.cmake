# Kernel file calls the pinned POSIX runtime leaves out or reports only through
# the filtered WARN log; the save/profile screens depend on them. Edits apply to
# the POSIX half of kernel_file.c only (the Windows half has the same text).
file(READ "${BLACK_MAC_RUNTIME}/src/kernel/kernel_file.c" black_file)
string(FIND "${black_file}" "NTSTATUS __stdcall xbox_NtReadFile(" black_file_posix REVERSE)
if(black_file_posix EQUAL -1)
    message(FATAL_ERROR "kernel_file.c POSIX anchor changed")
endif()
string(SUBSTRING "${black_file}" 0 ${black_file_posix} black_file_head)
string(SUBSTRING "${black_file}" ${black_file_posix} -1 black_file_tail)
function(black_file_replace anchor replacement)
    string(REPLACE "${anchor}" "" stripped "${black_file_tail}")
    string(LENGTH "${black_file_tail}" before)
    string(LENGTH "${stripped}" after)
    string(LENGTH "${anchor}" size)
    math(EXPR count "(${before}-${after})/${size}")
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "kernel_file.c anchor changed: ${anchor}")
    endif()
    string(REPLACE "${anchor}" "${replacement}" edited "${black_file_tail}")
    set(black_file_tail "${edited}" PARENT_SCOPE)
endfunction()
# FileAllocationInformation: NT reserves space; a size below the end of file
# truncates it, a larger one leaves the file as it is.
black_file_replace("        case XboxFileBasicInformation:\n            /* Setting file times is non-essential for the game; accept it. */"
"        case XboxFileAllocationInformation: {
            PXBOX_FILE_END_OF_FILE_INFORMATION info = (PXBOX_FILE_END_OF_FILE_INFORMATION)FileInformation;
            struct stat st;
            if (fstat(fd, &st) != 0) return STATUS_UNSUCCESSFUL;
            if (info->EndOfFile.QuadPart < 0) return STATUS_INVALID_PARAMETER;
            if (info->EndOfFile.QuadPart < st.st_size && ftruncate(fd, (off_t)info->EndOfFile.QuadPart) != 0)
                return STATUS_UNSUCCESSFUL;
            if (getenv(\"BLACK_SAVE_TRACE\"))
                fprintf(stderr, \"  [SAVE-TRACE] allocation %lld (size %lld) %s\\n\",
                    (long long)info->EndOfFile.QuadPart, (long long)st.st_size, w32_handle_path(FileHandle));
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileBasicInformation:
            /* Setting file times is non-essential for the game; accept it. */")
black_file_replace("                \"NtQueryVolumeInformationFile: unhandled class %d\", FsInformationClass);"
"                \"NtQueryVolumeInformationFile: unhandled class %d\", FsInformationClass);
            fprintf(stderr, \"  [FILE] NtQueryVolumeInformationFile: unhandled class %d\\n\", (int)FsInformationClass);")
black_file_replace("                \"NtQueryInformationFile: unhandled class %d\", FileInformationClass);"
"                \"NtQueryInformationFile: unhandled class %d\", FileInformationClass);
            fprintf(stderr, \"  [FILE] NtQueryInformationFile: unhandled class %d\\n\", (int)FileInformationClass);")
black_file_replace("    IoStatusBlock->Information = header_size + name_len;\n    return STATUS_SUCCESS;"
"    IoStatusBlock->Information = header_size + name_len;
    if (getenv(\"BLACK_SAVE_TRACE\"))
        fprintf(stderr, \"  [SAVE-TRACE] dir %s pattern=%s -> %s attrs=0x%x size=%lld length=%u\\n\",
            dpath ? dpath : \"?\", ctx->pattern, de->d_name, (unsigned)entry->FileAttributes,
            (long long)st.st_size, (unsigned)Length);
    return STATUS_SUCCESS;")
# A directory enumeration belongs to one open handle. The pinned table kept a
# DIR stream until it reached the end, keyed by the handle value, and the guest
# gets the same value back for its next open: a save list that stopped at the
# first save folder left the stream mid-directory, so the next list resumed past
# the folder and the profile screens found no profile ("no profiles to load",
# every slot "damaged"). Closing the handle now ends its enumeration.
black_file_replace("NTSTATUS __stdcall xbox_NtClose(HANDLE Handle)\n{\n    XBOX_TRACE(XBOX_LOG_FILE, \"NtClose(handle=%p)\", Handle);\n    if (Handle && Handle != INVALID_HANDLE_VALUE) {"
"void black_DirForget(HANDLE handle);
NTSTATUS __stdcall xbox_NtClose(HANDLE Handle)
{
    XBOX_TRACE(XBOX_LOG_FILE, \"NtClose(handle=%p)\", Handle);
    if (Handle && Handle != INVALID_HANDLE_VALUE) {
        black_DirForget(Handle);")
black_file_replace("static DIR_CONTEXT s_dir_contexts[MAX_DIR_CONTEXTS];\nstatic CRITICAL_SECTION s_dir_cs;\nstatic BOOL s_dir_cs_init = FALSE;\n"
"static DIR_CONTEXT s_dir_contexts[MAX_DIR_CONTEXTS];
static CRITICAL_SECTION s_dir_cs;
static BOOL s_dir_cs_init = FALSE;

/* Also called by the guest NtClose bridge, which closes handles directly. */
void black_DirForget(HANDLE handle)
{
    if (!s_dir_cs_init) return;
    EnterCriticalSection(&s_dir_cs);
    for (int i = 0; i < MAX_DIR_CONTEXTS; i++)
        if (s_dir_contexts[i].handle == handle) {
            if (s_dir_contexts[i].dir) closedir(s_dir_contexts[i].dir);
            s_dir_contexts[i].dir = NULL;
            s_dir_contexts[i].handle = NULL;
            if (getenv(\"BLACK_SAVE_TRACE\")) fprintf(stderr, \"  [SAVE-TRACE] dir enumeration closed with its handle\\n\");
        }
    LeaveCriticalSection(&s_dir_cs);
}
")
# BLACK_SAVE_TRACE: every read of a save file, with what it returned.
black_file_replace("                ((unsigned char *)Buffer)[at] = 0;\n        }\n    }\n\n    IoStatusBlock->Information = (ULONG_PTR)n;\n    if (n == 0 && Length > 0) {\n        IoStatusBlock->Status = STATUS_END_OF_FILE;"
"                ((unsigned char *)Buffer)[at] = 0;
        }
    }

    IoStatusBlock->Information = (ULONG_PTR)n;
    if (getenv(\"BLACK_SAVE_TRACE\")) {
        const char *black_path = w32_handle_path(FileHandle);
        if (black_path && strstr(black_path, \"/UserData/\"))
            fprintf(stderr, \"  [SAVE-TRACE] read %s at %lld len %u -> %lld event=%p apc=%p\\n\", black_path,
                (long long)start_pos, (unsigned)Length, (long long)n, Event, (void *)ApcRoutine);
    }
    if (n == 0 && Length > 0) {
        IoStatusBlock->Status = STATUS_END_OF_FILE;")
black_file_replace("    int fd = w32_handle_fd(FileHandle);\n    if (fd < 0)\n        return STATUS_INVALID_HANDLE;\n\n    struct stat st;"
"    int fd = w32_handle_fd(FileHandle);
    if (fd < 0)
        return STATUS_INVALID_HANDLE;
    if (getenv(\"BLACK_SAVE_TRACE\")) {
        const char *black_path = w32_handle_path(FileHandle);
        struct stat black_st;
        if (black_path && strstr(black_path, \"/UserData/\") && fstat(fd, &black_st) == 0)
            fprintf(stderr, \"  [SAVE-TRACE] query class %d %s size %lld\\n\", (int)FileInformationClass,
                black_path, (long long)black_st.st_size);
    }

    struct stat st;")
set(black_file "${black_file_head}${black_file_tail}")
set(black_file_dest "${CMAKE_BINARY_DIR}/runtime-overlay/kernel_file.c")
set(black_file_previous "")
if(EXISTS "${black_file_dest}")
    file(READ "${black_file_dest}" black_file_previous)
endif()
if(NOT black_file_previous STREQUAL black_file)
    file(WRITE "${black_file_dest}" "${black_file}")
endif()
get_target_property(black_kernel_file_sources xbox_kernel SOURCES)
list(REMOVE_ITEM black_kernel_file_sources kernel_file.c)
set_property(TARGET xbox_kernel PROPERTY SOURCES ${black_kernel_file_sources} "${black_file_dest}")
target_include_directories(xbox_kernel PRIVATE "${BLACK_MAC_RUNTIME}/src/kernel")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BLACK_MAC_RUNTIME}/src/kernel/kernel_file.c")
