#include <rtthread.h>
#include <string.h>
#include "feathertalk_storage.h"

#ifdef RT_USING_DFS
#include <dfs_fs.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>

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

#else

int ft_storage_get_volume(const char *mount_path,
                          ft_storage_volume_info_t *info)
{
    RT_UNUSED(mount_path);
    if (info != RT_NULL) rt_memset(info, 0, sizeof(*info));
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

#endif
