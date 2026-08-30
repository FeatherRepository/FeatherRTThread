#include <rtthread.h>
#include <string.h>
#include "board_storage.h"
#include "feathertalk_storage.h"

#ifdef RT_USING_DFS
#include <dfs_fs.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#define FT_STORAGE_OPERATION_MAX_DEPTH 12U
#define FT_STORAGE_COPY_BUFFER_SIZE 4096U

static bool storage_mount_matches(const char *mount_path,
                                  const struct dfs_filesystem *fs)
{
    return fs != RT_NULL && fs->path != RT_NULL && fs->ops != RT_NULL &&
           fs->ops->name != RT_NULL && strcmp(fs->path, mount_path) == 0;
}

int ft_storage_get_volume(const char *mount_path,
                          ft_storage_volume_info_t *info)
{
    struct dfs_filesystem *fs;
    struct statfs capacity;

    if (mount_path == RT_NULL || info == RT_NULL)
        return -RT_EINVAL;

    rt_memset(info, 0, sizeof(*info));
    fs = dfs_filesystem_lookup(mount_path);
    if (!storage_mount_matches(mount_path, fs))
        return -RT_ENOENT;

    info->mounted = true;
    rt_strncpy(info->filesystem, fs->ops->name,
               sizeof(info->filesystem) - 1U);
    if (dfs_statfs(mount_path, &capacity) == RT_EOK)
    {
        info->total_bytes = (uint64_t)capacity.f_blocks * capacity.f_bsize;
        info->free_bytes = (uint64_t)capacity.f_bfree * capacity.f_bsize;
    }
    return RT_EOK;
}

int ft_storage_get_device_info(ft_storage_device_info_t *info)
{
    board_sdcard_info_t board_info;
    ft_storage_volume_info_t volume;
    size_t copy_count;
    size_t index;
    int result;

    if (info == RT_NULL) return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    result = board_sdcard_get_info(&board_info);
    if (result != RT_EOK) return result;

    info->present = board_info.present;
    info->mounted = board_info.mounted;
    info->usb_exported = board_info.exported;
    info->busy = board_info.transitioning;
    info->can_format = board_info.present && !board_info.exported &&
                       !board_info.transitioning;
    info->partition_truncated = board_info.partition_truncated;
    rt_strncpy(info->device, board_info.device_name,
               sizeof(info->device) - 1U);
    rt_strncpy(info->mounted_device, board_info.mounted_device,
               sizeof(info->mounted_device) - 1U);
    info->sector_count = board_info.sector_count;
    info->bytes_per_sector = board_info.bytes_per_sector;
    info->erase_block_size = board_info.erase_block_size;
    info->total_bytes = board_info.sector_count *
                        (uint64_t)board_info.bytes_per_sector;
    info->partition_scheme =
        (ft_storage_partition_scheme_t)board_info.partition_scheme;
    info->partition_count = board_info.partition_count;
    copy_count = board_info.partition_count;
    if (copy_count > FT_STORAGE_MAX_PARTITIONS)
        copy_count = FT_STORAGE_MAX_PARTITIONS;
    for (index = 0U; index < copy_count; index++)
    {
        info->partitions[index].first_sector =
            board_info.partitions[index].first_sector;
        info->partitions[index].sector_count =
            board_info.partitions[index].sector_count;
        info->partitions[index].mbr_type =
            board_info.partitions[index].mbr_type;
    }

    if (ft_storage_get_volume(FT_STORAGE_SD_MOUNT_PATH, &volume) == RT_EOK)
    {
        info->mounted = true;
        rt_strncpy(info->filesystem, volume.filesystem,
                   sizeof(info->filesystem) - 1U);
        info->volume_total_bytes = volume.total_bytes;
        info->volume_free_bytes = volume.free_bytes;
    }
    return RT_EOK;
}

int ft_storage_format_sd(void)
{
    return board_sdcard_format_fat();
}

int ft_storage_get_flash_info(ft_storage_device_info_t *info)
{
    board_flash_storage_info_t board_info;
    ft_storage_volume_info_t volume;
    int result;

    if (info == RT_NULL) return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    result = board_flash_storage_get_info(&board_info);
    if (result != RT_EOK) return result;
    info->present = board_info.present;
    info->mounted = board_info.mounted;
    info->usb_exported = board_info.exported;
    info->busy = board_info.transitioning;
    info->can_format = board_info.present && !board_info.exported &&
                       !board_info.transitioning;
    rt_strncpy(info->device, board_info.device_name,
               sizeof(info->device) - 1U);
    rt_strncpy(info->mounted_device, board_info.device_name,
               sizeof(info->mounted_device) - 1U);
    info->sector_count = board_info.sector_count;
    info->bytes_per_sector = board_info.bytes_per_sector;
    info->erase_block_size = board_info.erase_block_size;
    info->total_bytes = board_info.sector_count *
                        (uint64_t)board_info.bytes_per_sector;
    if (ft_storage_get_volume(FT_STORAGE_FLASH_MOUNT_PATH, &volume) == RT_EOK)
    {
        info->mounted = true;
        rt_strncpy(info->filesystem, volume.filesystem,
                   sizeof(info->filesystem) - 1U);
        info->volume_total_bytes = volume.total_bytes;
        info->volume_free_bytes = volume.free_bytes;
    }
    return RT_EOK;
}

int ft_storage_format_flash(void)
{
    return board_flash_storage_format_fat();
}

int ft_storage_join_path(const char *parent, const char *name,
                         char *path, size_t path_size)
{
    int written;
    size_t parent_length;

    if (parent == RT_NULL || name == RT_NULL || path == RT_NULL ||
        path_size == 0U || name[0] == '\0' || strchr(name, '/') != RT_NULL)
        return -RT_EINVAL;

    parent_length = strlen(parent);
    written = rt_snprintf(path, path_size, "%s%s%s", parent,
                          parent_length > 0U && parent[parent_length - 1U] == '/' ? "" : "/",
                          name);
    if (written < 0 || (size_t)written >= path_size)
        return -RT_EFULL;
    return RT_EOK;
}

bool ft_storage_parent_path(char *path, const char *mount_path)
{
    char *separator;
    size_t root_length;

    if (path == RT_NULL || mount_path == RT_NULL)
        return false;
    root_length = strlen(mount_path);
    if (root_length == 1U && mount_path[0] == '/')
    {
        if (path[0] != '/' || path[1] == '\0') return false;
        separator = strrchr(path, '/');
        if (separator == path)
            path[1] = '\0';
        else if (separator != RT_NULL)
            *separator = '\0';
        return separator != RT_NULL;
    }
    if (strncmp(path, mount_path, root_length) != 0 ||
        (path[root_length] != '\0' && path[root_length] != '/'))
        return false;
    if (strlen(path) <= root_length)
        return false;

    separator = strrchr(path, '/');
    if (separator == RT_NULL || separator < path + root_length)
        return false;
    if (separator == path + root_length)
        path[root_length] = '\0';
    else
        *separator = '\0';
    return true;
}

static ft_storage_entry_type_t storage_entry_type(const char *directory,
                                                  const struct dirent *dirent,
                                                  uint64_t *size_bytes)
{
    char full_path[FT_STORAGE_PATH_MAX];
    struct stat status;

    *size_bytes = 0U;
    if (dirent->d_type == DT_DIR)
        return FT_STORAGE_ENTRY_DIRECTORY;
    if (dirent->d_type == DT_REG)
    {
        if (ft_storage_join_path(directory, dirent->d_name, full_path,
                                 sizeof(full_path)) == RT_EOK &&
            stat(full_path, &status) == 0)
            *size_bytes = (uint64_t)status.st_size;
        return FT_STORAGE_ENTRY_FILE;
    }

    if (ft_storage_join_path(directory, dirent->d_name, full_path,
                             sizeof(full_path)) != RT_EOK ||
        stat(full_path, &status) != 0)
        return FT_STORAGE_ENTRY_FILE;
    if (S_ISDIR(status.st_mode))
        return FT_STORAGE_ENTRY_DIRECTORY;
    *size_bytes = (uint64_t)status.st_size;
    return FT_STORAGE_ENTRY_FILE;
}

int ft_storage_list(const char *path, ft_storage_entry_type_t filter,
                    ft_storage_entry_cb_t callback, void *context)
{
    DIR *directory;
    struct dirent *dirent;
    int count = 0;

    if (path == RT_NULL || callback == RT_NULL || filter > FT_STORAGE_ENTRY_ANY)
        return -RT_EINVAL;

    directory = opendir(path);
    if (directory == RT_NULL)
        return -RT_ENOENT;

    while ((dirent = readdir(directory)) != RT_NULL)
    {
        ft_storage_entry_t entry;
        size_t name_length;

        if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
            continue;
        name_length = strlen(dirent->d_name);
        if (name_length == 0U || name_length >= sizeof(entry.name))
            continue;

        rt_memset(&entry, 0, sizeof(entry));
        entry.type = storage_entry_type(path, dirent, &entry.size_bytes);
        if (filter != FT_STORAGE_ENTRY_ANY && entry.type != filter)
            continue;
        rt_memcpy(entry.name, dirent->d_name, name_length + 1U);
        count++;
        if (!callback(&entry, context))
            break;
    }
    closedir(directory);
    return count;
}

int ft_storage_read_preview(const char *path, uint8_t *buffer,
                            size_t buffer_size, bool *binary,
                            uint64_t *file_size)
{
    struct stat status;
    int file;
    int read_size;
    size_t i;

    if (path == RT_NULL || buffer == RT_NULL || buffer_size < 2U ||
        binary == RT_NULL || file_size == RT_NULL)
        return -RT_EINVAL;
    if (stat(path, &status) != 0 || S_ISDIR(status.st_mode))
        return -RT_ENOENT;

    file = open(path, O_RDONLY, 0);
    if (file < 0)
        return -RT_EIO;
    read_size = read(file, buffer, buffer_size - 1U);
    close(file);
    if (read_size < 0)
        return -RT_EIO;

    buffer[read_size] = '\0';
    *file_size = (uint64_t)status.st_size;
    *binary = false;
    for (i = 0U; i < (size_t)read_size; i++)
    {
        uint8_t value = buffer[i];
        if (value == 0U || value < 0x09U ||
            (value > 0x0DU && value < 0x20U))
        {
            *binary = true;
            break;
        }
    }
    return read_size;
}

static bool storage_path_is_deletable(const char *path)
{
    static const char *roots[] =
    {
        FT_STORAGE_FLASH_MOUNT_PATH,
        FT_STORAGE_SD_MOUNT_PATH,
    };
    size_t index;
    size_t path_length;

    if (path == RT_NULL || path[0] != '/') return false;
    path_length = strlen(path);
    if (strstr(path, "/../") != RT_NULL || strstr(path, "/./") != RT_NULL ||
        strstr(path, "//") != RT_NULL ||
        (path_length >= 3U && strcmp(path + path_length - 3U, "/..") == 0) ||
        (path_length >= 2U && strcmp(path + path_length - 2U, "/.") == 0))
        return false;
    for (index = 0U; index < sizeof(roots) / sizeof(roots[0]); index++)
    {
        size_t root_length = strlen(roots[index]);
        if (strncmp(path, roots[index], root_length) == 0 &&
            path[root_length] == '/' && path[root_length + 1U] != '\0')
            return true;
    }
    return false;
}

static bool storage_path_is_managed_directory(const char *path)
{
    static const char *roots[] =
    {
        FT_STORAGE_FLASH_MOUNT_PATH,
        FT_STORAGE_SD_MOUNT_PATH,
    };
    size_t index;
    size_t path_length;

    if (path == RT_NULL || path[0] != '/' ||
        strstr(path, "/../") != RT_NULL || strstr(path, "/./") != RT_NULL ||
        strstr(path, "//") != RT_NULL)
        return false;
    path_length = strlen(path);
    if ((path_length >= 3U && strcmp(path + path_length - 3U, "/..") == 0) ||
        (path_length >= 2U && strcmp(path + path_length - 2U, "/.") == 0))
        return false;
    for (index = 0U; index < sizeof(roots) / sizeof(roots[0]); index++)
    {
        size_t root_length = strlen(roots[index]);
        if (strcmp(path, roots[index]) == 0 ||
            (strncmp(path, roots[index], root_length) == 0 &&
             path[root_length] == '/' && path[root_length + 1U] != '\0'))
            return true;
    }
    return false;
}

static int storage_delete_tree(const char *path, uint32_t depth)
{
    struct stat status;
    DIR *directory;
    struct dirent *entry;

    if (depth > FT_STORAGE_OPERATION_MAX_DEPTH) return -RT_EBUSY;
    if (stat(path, &status) != 0) return -RT_ENOENT;
    if (!S_ISDIR(status.st_mode))
        return unlink(path) == 0 ? RT_EOK : -RT_EIO;

    directory = opendir(path);
    if (directory == RT_NULL) return -RT_EIO;
    while ((entry = readdir(directory)) != RT_NULL)
    {
        char child[FT_STORAGE_PATH_MAX];
        int result;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (ft_storage_join_path(path, entry->d_name, child,
                                 sizeof(child)) != RT_EOK)
        {
            closedir(directory);
            return -RT_EFULL;
        }
        result = storage_delete_tree(child, depth + 1U);
        if (result != RT_EOK)
        {
            closedir(directory);
            return result;
        }
    }
    closedir(directory);
    return rmdir(path) == 0 ? RT_EOK : -RT_EBUSY;
}

int ft_storage_delete_path(const char *path)
{
    if (!storage_path_is_deletable(path)) return -RT_EINVAL;
    return storage_delete_tree(path, 0U);
}

static bool storage_name_is_valid(const char *name)
{
    static const char forbidden[] = "\\\"*/:<>?|";
    size_t length;
    size_t index;

    if (name == RT_NULL || name[0] == '\0' ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;
    length = strlen(name);
    if (length >= FT_STORAGE_NAME_MAX || name[0] == ' ' ||
        name[length - 1U] == ' ' || name[length - 1U] == '.')
        return false;
    for (index = 0U; index < length; index++)
    {
        unsigned char value = (unsigned char)name[index];
        if (value < 0x20U || strchr(forbidden, (int)value) != RT_NULL)
            return false;
    }
    return true;
}

int ft_storage_create_directory(const char *parent, const char *name,
                                char *result_path, size_t result_path_size)
{
    struct stat status;
    char path[FT_STORAGE_PATH_MAX];

    if (!storage_path_is_managed_directory(parent) ||
        !storage_name_is_valid(name) || result_path == RT_NULL ||
        result_path_size == 0U)
        return -RT_EINVAL;
    result_path[0] = '\0';
    if (stat(parent, &status) != 0 || !S_ISDIR(status.st_mode))
        return -RT_ENOENT;
    if (ft_storage_join_path(parent, name, path, sizeof(path)) != RT_EOK)
        return -RT_EFULL;
    if (stat(path, &status) == 0) return -RT_EBUSY;
    if (mkdir(path, 0700) != 0) return -RT_EIO;
    rt_strncpy(result_path, path, result_path_size - 1U);
    result_path[result_path_size - 1U] = '\0';
    return RT_EOK;
}

int ft_storage_rename_path(const char *path, const char *new_name,
                           char *result_path, size_t result_path_size)
{
    struct stat status;
    const char *separator;
    const char *old_name;
    char parent[FT_STORAGE_PATH_MAX];
    char destination[FT_STORAGE_PATH_MAX];
    size_t parent_length;

    if (!storage_path_is_deletable(path) || !storage_name_is_valid(new_name) ||
        result_path == RT_NULL || result_path_size == 0U)
        return -RT_EINVAL;
    result_path[0] = '\0';
    if (stat(path, &status) != 0) return -RT_ENOENT;
    separator = strrchr(path, '/');
    if (separator == RT_NULL || separator == path) return -RT_EINVAL;
    old_name = separator + 1U;
    if (strcmp(old_name, new_name) == 0)
    {
        rt_strncpy(result_path, path, result_path_size - 1U);
        result_path[result_path_size - 1U] = '\0';
        return RT_EOK;
    }
    parent_length = (size_t)(separator - path);
    if (parent_length >= sizeof(parent)) return -RT_EFULL;
    rt_memcpy(parent, path, parent_length);
    parent[parent_length] = '\0';
    if (!storage_path_is_managed_directory(parent) ||
        ft_storage_join_path(parent, new_name, destination,
                             sizeof(destination)) != RT_EOK)
        return -RT_EINVAL;
    if (stat(destination, &status) == 0) return -RT_EBUSY;
    if (rename(path, destination) != 0) return -RT_EIO;
    rt_strncpy(result_path, destination, result_path_size - 1U);
    result_path[result_path_size - 1U] = '\0';
    return RT_EOK;
}

static int storage_copy_file(const char *source, const char *destination)
{
    uint8_t *buffer;
    int input;
    int output;
    int result = RT_EOK;

    buffer = rt_malloc(FT_STORAGE_COPY_BUFFER_SIZE);
    if (buffer == RT_NULL) return -RT_ENOMEM;
    input = open(source, O_RDONLY, 0);
    if (input < 0)
    {
        rt_free(buffer);
        return -RT_EIO;
    }
    output = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0)
    {
        close(input);
        rt_free(buffer);
        return -RT_EBUSY;
    }
    for (;;)
    {
        int read_size = read(input, buffer, FT_STORAGE_COPY_BUFFER_SIZE);
        int written = 0;
        if (read_size < 0)
        {
            result = -RT_EIO;
            break;
        }
        if (read_size == 0) break;
        while (written < read_size)
        {
            int write_size = write(output, buffer + written,
                                   (size_t)(read_size - written));
            if (write_size <= 0)
            {
                result = -RT_EFULL;
                break;
            }
            written += write_size;
        }
        if (result != RT_EOK) break;
    }
    close(output);
    close(input);
    rt_free(buffer);
    if (result != RT_EOK) unlink(destination);
    return result;
}

static int storage_copy_tree(const char *source, const char *destination,
                             uint32_t depth)
{
    struct stat status;
    DIR *directory;
    struct dirent *entry;

    if (depth > FT_STORAGE_OPERATION_MAX_DEPTH) return -RT_EBUSY;
    if (stat(source, &status) != 0) return -RT_ENOENT;
    if (!S_ISDIR(status.st_mode))
        return storage_copy_file(source, destination);
    if (mkdir(destination, 0700) != 0) return -RT_EBUSY;
    directory = opendir(source);
    if (directory == RT_NULL)
    {
        rmdir(destination);
        return -RT_EIO;
    }
    while ((entry = readdir(directory)) != RT_NULL)
    {
        char source_child[FT_STORAGE_PATH_MAX];
        char destination_child[FT_STORAGE_PATH_MAX];
        int result;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (ft_storage_join_path(source, entry->d_name, source_child,
                                 sizeof(source_child)) != RT_EOK ||
            ft_storage_join_path(destination, entry->d_name, destination_child,
                                 sizeof(destination_child)) != RT_EOK)
        {
            closedir(directory);
            (void)storage_delete_tree(destination, 0U);
            return -RT_EFULL;
        }
        result = storage_copy_tree(source_child, destination_child, depth + 1U);
        if (result != RT_EOK)
        {
            closedir(directory);
            (void)storage_delete_tree(destination, 0U);
            return result;
        }
    }
    closedir(directory);
    return RT_EOK;
}

static bool storage_path_is_same_or_child(const char *path,
                                          const char *directory)
{
    size_t length;
    size_t path_length;
    if (path == RT_NULL || directory == RT_NULL) return false;
    length = strlen(directory);
    path_length = strlen(path);
    return strcmp(path, directory) == 0 ||
           (path_length > length && strncmp(path, directory, length) == 0 &&
            path[length] == '/');
}

static const char *storage_basename(const char *path)
{
    const char *slash = path != RT_NULL ? strrchr(path, '/') : RT_NULL;
    return slash != RT_NULL ? slash + 1U : path;
}

static bool storage_parent_matches(const char *path, const char *directory)
{
    const char *slash;
    size_t parent_length;
    if (path == RT_NULL || directory == RT_NULL) return false;
    slash = strrchr(path, '/');
    if (slash == RT_NULL) return false;
    parent_length = (size_t)(slash - path);
    return strlen(directory) == parent_length &&
           strncmp(path, directory, parent_length) == 0;
}

static int storage_choose_destination(const char *source,
                                      const char *destination_directory,
                                      char *destination,
                                      size_t destination_size)
{
    const char *name = storage_basename(source);
    const char *dot = strrchr(name, '.');
    char candidate[FT_STORAGE_NAME_MAX];
    size_t base_length;
    unsigned long attempt;
    struct stat status;

    if (name == RT_NULL || name[0] == '\0') return -RT_EINVAL;
    base_length = dot != RT_NULL && dot != name ? (size_t)(dot - name) : strlen(name);
    for (attempt = 0U; attempt < 1000U; attempt++)
    {
        if (attempt == 0U)
            rt_snprintf(candidate, sizeof(candidate), "%s", name);
        else if (attempt == 1U)
            rt_snprintf(candidate, sizeof(candidate), "%.*s-copy%s",
                        (int)base_length, name, dot != RT_NULL && dot != name ? dot : "");
        else
            rt_snprintf(candidate, sizeof(candidate), "%.*s-copy-%lu%s",
                        (int)base_length, name, attempt,
                        dot != RT_NULL && dot != name ? dot : "");
        if (ft_storage_join_path(destination_directory, candidate, destination,
                                 destination_size) != RT_EOK)
            return -RT_EFULL;
        if (stat(destination, &status) != 0) return RT_EOK;
    }
    return -RT_EFULL;
}

int ft_storage_paste_path(const char *source, const char *destination_directory,
                          bool move, char *result_path,
                          size_t result_path_size)
{
    struct stat source_status;
    struct stat destination_status;
    char destination[FT_STORAGE_PATH_MAX];
    int result;

    if (!storage_path_is_deletable(source) ||
        !storage_path_is_managed_directory(destination_directory) ||
        result_path == RT_NULL || result_path_size == 0U)
        return -RT_EINVAL;
    result_path[0] = '\0';
    if (stat(source, &source_status) != 0 ||
        stat(destination_directory, &destination_status) != 0 ||
        !S_ISDIR(destination_status.st_mode))
        return -RT_ENOENT;
    if (S_ISDIR(source_status.st_mode) &&
        storage_path_is_same_or_child(destination_directory, source))
        return -RT_EINVAL;
    if (move && storage_parent_matches(source, destination_directory))
    {
        rt_strncpy(result_path, source, result_path_size - 1U);
        result_path[result_path_size - 1U] = '\0';
        return RT_EOK;
    }
    result = storage_choose_destination(source, destination_directory,
                                        destination, sizeof(destination));
    if (result != RT_EOK) return result;
    if (move && rename(source, destination) == 0)
        result = RT_EOK;
    else
    {
        result = storage_copy_tree(source, destination, 0U);
        if (result == RT_EOK && move)
        {
            result = storage_delete_tree(source, 0U);
            if (result != RT_EOK)
            {
                (void)storage_delete_tree(destination, 0U);
                return result;
            }
        }
    }
    if (result != RT_EOK) return result;
    rt_strncpy(result_path, destination, result_path_size - 1U);
    result_path[result_path_size - 1U] = '\0';
    return RT_EOK;
}

#ifdef FEATHERTALK_UI_TEST_MODE
bool ft_storage_test_delete_contract(void)
{
    char file_path[FT_STORAGE_PATH_MAX];
    char directory_path[FT_STORAGE_PATH_MAX];
    char nested_path[FT_STORAGE_PATH_MAX];
    struct stat status;
    unsigned long nonce = (unsigned long)rt_tick_get();
    int file;
    bool file_deleted;
    bool directory_deleted;

    if (ft_storage_delete_path(FT_STORAGE_FLASH_MOUNT_PATH) != -RT_EINVAL ||
        ft_storage_delete_path(FT_STORAGE_SD_MOUNT_PATH) != -RT_EINVAL)
        return false;
    rt_snprintf(file_path, sizeof(file_path),
                FT_STORAGE_FLASH_MOUNT_PATH "/.ft-delete-%08lx.tmp", nonce);
    rt_snprintf(directory_path, sizeof(directory_path),
                FT_STORAGE_FLASH_MOUNT_PATH "/.ft-delete-%08lx.dir", nonce);
    file = open(file_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) return false;
    if (write(file, "delete-contract", 15U) != 15)
    {
        close(file);
        unlink(file_path);
        return false;
    }
    close(file);
    file_deleted = ft_storage_delete_path(file_path) == RT_EOK &&
                   stat(file_path, &status) != 0;
    if (mkdir(directory_path, 0700) != 0)
    {
        if (!file_deleted) unlink(file_path);
        return false;
    }
    if (ft_storage_join_path(directory_path, "nested.tmp", nested_path,
                             sizeof(nested_path)) != RT_EOK)
    {
        rmdir(directory_path);
        return false;
    }
    file = open(nested_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0)
    {
        rmdir(directory_path);
        return false;
    }
    if (write(file, "nested", 6U) != 6)
    {
        close(file);
        unlink(nested_path);
        rmdir(directory_path);
        return false;
    }
    close(file);
    directory_deleted = ft_storage_delete_path(directory_path) == RT_EOK &&
                        stat(directory_path, &status) != 0;
    if (!file_deleted) unlink(file_path);
    if (!directory_deleted)
    {
        unlink(nested_path);
        rmdir(directory_path);
    }
    return file_deleted && directory_deleted;
}

bool ft_storage_test_clipboard_contract(void)
{
    char source[FT_STORAGE_PATH_MAX];
    char directory[FT_STORAGE_PATH_MAX];
    char copied[FT_STORAGE_PATH_MAX];
    char moved[FT_STORAGE_PATH_MAX];
    char expected_copy[FT_STORAGE_PATH_MAX];
    struct stat status;
    unsigned long nonce = (unsigned long)rt_tick_get();
    int file;
    bool passed = false;

    rt_memset(source, 0, sizeof(source));
    rt_memset(directory, 0, sizeof(directory));
    rt_memset(copied, 0, sizeof(copied));
    rt_memset(moved, 0, sizeof(moved));
    rt_memset(expected_copy, 0, sizeof(expected_copy));
    rt_snprintf(source, sizeof(source),
                FT_STORAGE_FLASH_MOUNT_PATH "/.ft-clip-%08lx.tmp", nonce);
    rt_snprintf(directory, sizeof(directory),
                FT_STORAGE_FLASH_MOUNT_PATH "/.ft-clip-%08lx.dir", nonce);
    if (mkdir(directory, 0700) != 0) return false;
    file = open(source, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) goto cleanup;
    if (write(file, "clipboard-contract", 18U) != 18)
    {
        close(file);
        goto cleanup;
    }
    close(file);
    if (ft_storage_paste_path(source, directory, false,
                              copied, sizeof(copied)) != RT_EOK ||
        stat(source, &status) != 0 || stat(copied, &status) != 0)
        goto cleanup;
    if (ft_storage_paste_path(copied, FT_STORAGE_FLASH_MOUNT_PATH, true,
                              moved, sizeof(moved)) != RT_EOK ||
        stat(copied, &status) == 0 || stat(moved, &status) != 0)
        goto cleanup;
    if (ft_storage_paste_path(directory, directory, false,
                              expected_copy, sizeof(expected_copy)) != -RT_EINVAL)
        goto cleanup;
    passed = true;

cleanup:
    if (source[0] != '\0' && stat(source, &status) == 0)
        (void)ft_storage_delete_path(source);
    if (directory[0] != '\0' && stat(directory, &status) == 0)
        (void)ft_storage_delete_path(directory);
    if (moved[0] != '\0' && stat(moved, &status) == 0)
        (void)ft_storage_delete_path(moved);
    return passed;
}

bool ft_storage_test_name_contract(void)
{
    char directory[FT_STORAGE_PATH_MAX];
    char renamed[FT_STORAGE_PATH_MAX];
    char expected[FT_STORAGE_PATH_MAX];
    char name[FT_STORAGE_NAME_MAX];
    char renamed_name[FT_STORAGE_NAME_MAX];
    struct stat status;
    unsigned long nonce = (unsigned long)rt_tick_get();
    bool passed = false;

    rt_memset(directory, 0, sizeof(directory));
    rt_memset(renamed, 0, sizeof(renamed));
    rt_memset(expected, 0, sizeof(expected));
    rt_snprintf(name, sizeof(name), ".ft-name-%08lx", nonce);
    rt_snprintf(renamed_name, sizeof(renamed_name),
                ".ft-renamed-%08lx", nonce);
    if (ft_storage_create_directory(FT_STORAGE_FLASH_MOUNT_PATH, name,
                                    directory, sizeof(directory)) != RT_EOK ||
        stat(directory, &status) != 0 || !S_ISDIR(status.st_mode))
        goto cleanup;
    if (ft_storage_rename_path(directory, renamed_name,
                               renamed, sizeof(renamed)) != RT_EOK ||
        stat(directory, &status) == 0 || stat(renamed, &status) != 0 ||
        !S_ISDIR(status.st_mode))
        goto cleanup;
    if (ft_storage_join_path(FT_STORAGE_FLASH_MOUNT_PATH, renamed_name,
                             expected, sizeof(expected)) != RT_EOK ||
        strcmp(renamed, expected) != 0)
        goto cleanup;
    if (ft_storage_rename_path(FT_STORAGE_FLASH_MOUNT_PATH, "invalid",
                               expected, sizeof(expected)) != -RT_EINVAL ||
        ft_storage_create_directory(FT_STORAGE_FLASH_MOUNT_PATH, "bad/name",
                                    expected, sizeof(expected)) != -RT_EINVAL)
        goto cleanup;
    passed = true;

cleanup:
    if (renamed[0] != '\0' && stat(renamed, &status) == 0)
        (void)ft_storage_delete_path(renamed);
    if (directory[0] != '\0' && stat(directory, &status) == 0)
        (void)ft_storage_delete_path(directory);
    return passed;
}
#endif

#else

int ft_storage_get_volume(const char *mount_path,
                          ft_storage_volume_info_t *info)
{
    RT_UNUSED(mount_path);
    if (info != RT_NULL) rt_memset(info, 0, sizeof(*info));
    return -RT_ENOSYS;
}

int ft_storage_get_device_info(ft_storage_device_info_t *info)
{
    if (info != RT_NULL) rt_memset(info, 0, sizeof(*info));
    return -RT_ENOSYS;
}

int ft_storage_format_sd(void)
{
    return -RT_ENOSYS;
}

int ft_storage_get_flash_info(ft_storage_device_info_t *info)
{
    if (info != RT_NULL) rt_memset(info, 0, sizeof(*info));
    return -RT_ENOSYS;
}

int ft_storage_format_flash(void)
{
    return -RT_ENOSYS;
}

int ft_storage_list(const char *path, ft_storage_entry_type_t filter,
                    ft_storage_entry_cb_t callback, void *context)
{
    RT_UNUSED(path);
    RT_UNUSED(filter);
    RT_UNUSED(callback);
    RT_UNUSED(context);
    return -RT_ENOSYS;
}

int ft_storage_join_path(const char *parent, const char *name,
                         char *path, size_t path_size)
{
    RT_UNUSED(parent);
    RT_UNUSED(name);
    RT_UNUSED(path);
    RT_UNUSED(path_size);
    return -RT_ENOSYS;
}

bool ft_storage_parent_path(char *path, const char *mount_path)
{
    RT_UNUSED(path);
    RT_UNUSED(mount_path);
    return false;
}

int ft_storage_read_preview(const char *path, uint8_t *buffer,
                            size_t buffer_size, bool *binary,
                            uint64_t *file_size)
{
    RT_UNUSED(path);
    RT_UNUSED(buffer);
    RT_UNUSED(buffer_size);
    RT_UNUSED(binary);
    RT_UNUSED(file_size);
    return -RT_ENOSYS;
}

int ft_storage_delete_path(const char *path)
{
    RT_UNUSED(path);
    return -RT_ENOSYS;
}

int ft_storage_create_directory(const char *parent, const char *name,
                                char *result_path, size_t result_path_size)
{
    RT_UNUSED(parent);
    RT_UNUSED(name);
    RT_UNUSED(result_path);
    RT_UNUSED(result_path_size);
    return -RT_ENOSYS;
}

int ft_storage_rename_path(const char *path, const char *new_name,
                           char *result_path, size_t result_path_size)
{
    RT_UNUSED(path);
    RT_UNUSED(new_name);
    RT_UNUSED(result_path);
    RT_UNUSED(result_path_size);
    return -RT_ENOSYS;
}

int ft_storage_paste_path(const char *source, const char *destination_directory,
                          bool move, char *result_path,
                          size_t result_path_size)
{
    RT_UNUSED(source);
    RT_UNUSED(destination_directory);
    RT_UNUSED(move);
    RT_UNUSED(result_path);
    RT_UNUSED(result_path_size);
    return -RT_ENOSYS;
}

#ifdef FEATHERTALK_UI_TEST_MODE
bool ft_storage_test_delete_contract(void)
{
    return false;
}


bool ft_storage_test_clipboard_contract(void)
{
    return false;
}

bool ft_storage_test_name_contract(void)
{
    return false;
}
#endif

#endif
