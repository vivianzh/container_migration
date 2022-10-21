/*
 * Container migration
 *
 * conainer_migration is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; version 2.
 *
 * container_migration is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should find a copy of v2 of the GNU General Public License somewhere
 * on your Linux system; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <getopt.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <numa.h>
#include <numaif.h>

#define CGPATH_MAX 200
#define PIDNUM_MAX 10

struct option opt[] = {
    {"help", 0, 0, 'h' },
    { 0 }
};

void print_usage(void)
{
    printf("usage: container_migration container_id from-nodes to-nodes\n");
    exit(1);
}

int find_path(char *dir_path, char *file_name, char *cgpath)
{
    DIR *dir;
    struct dirent *ptr;
    char path[CGPATH_MAX];
    int ret = 1;
    
    memset(path, 0, sizeof(path));

    if((dir=opendir(dir_path))== NULL) {
        printf("Failed to open dir %s\n", dir_path);
        return 1;
    }

    while((ptr = readdir(dir)) != NULL) {
        if (ptr->d_type == 4) {  //dir
            if(0 == strcmp(ptr->d_name, ".") || 0 == strcmp(ptr->d_name, ".."))
                continue;
            if(strstr(ptr->d_name, file_name) != NULL) {
                strcpy(cgpath, dir_path);
                strcat(cgpath,"/");
                strcat(cgpath,ptr->d_name);
                ret = 0;
                break;
            } else {
                strcpy(path, dir_path);
                strcat(path,"/");
                strcat(path,ptr->d_name);
                ret = find_path(path, file_name, cgpath);
                if (ret == 0)
                    break;
            }
        }
    }
    closedir(dir);
    return ret;
}

int main(int argc, char *argv[])
{
    int c;
    int ret;
    int pid;
    struct bitmask *nodes_from;
    struct bitmask *nodes_to;
    char cgpath[CGPATH_MAX];
    char cgpath_freezer[CGPATH_MAX];
    char cgpath_pids[CGPATH_MAX];
    char container_id[CGPATH_MAX];
    char pid_num[PIDNUM_MAX];
    FILE *fp;

    while ((c = getopt_long(argc,argv,"h", opt, NULL)) != -1) {
        switch (c) {
            default:
                print_usage();
        }
    }

    argv += optind;
    argc -= optind;

    if (argc != 3)
        print_usage();

    // Check numa
    if (numa_available() < 0) {
        printf("The system does not support NUMA functionality, exit\n");
        exit(1);
    }

    // Check parameters
    strcpy(container_id, argv[0]); 
    nodes_from = numa_parse_nodestring(argv[1]);
    if (!nodes_from) {
        printf ("<%s> is invalid\n", argv[1]);
        exit(1);
    }
    nodes_to = numa_parse_nodestring(argv[2]);
    if (!nodes_to) {
        printf ("<%s> is invalid\n", argv[2]);
        exit(1);
    }

    printf("container_id:%s\n", container_id);
    memset(cgpath, 0, sizeof(cgpath));

    // Freeze the CPU
    ret = find_path("/sys/fs/cgroup/freezer/", container_id, cgpath);
    if (ret == 0) {
        strcpy(cgpath_freezer, cgpath);
        strcat(cgpath_freezer, "/freezer.state");
        printf("cgpath_freezer:%s\n", cgpath_freezer);
        fp = fopen(cgpath_freezer, "w");
        if (fp == NULL) {
            printf("Failed to open file %s\n", cgpath_freezer);
            return 1;
        }
    } else {
        printf("Failed to find cgroup file, exit\n");
        return 1;
    }
    printf("Write FROZEN\n");
    ret = fputs("FROZEN", fp);
    if (ret < 0)
        printf("Failed to write FROZEN");
    fclose(fp);

    // Get the PID and migrate pages
    strcpy(cgpath_pids, cgpath);
    strcat(cgpath_pids, "/cgroup.procs");
    printf("cgpath_pids:%s\n", cgpath_pids);
    fp = fopen(cgpath_pids, "r");
    if (fp == NULL) {
        printf("Failed to open file %s\n", cgpath_pids);
    } else {
        while ((fgets(pid_num, PIDNUM_MAX - 1, fp))!= NULL) {
            pid = atoi(pid_num);
            printf("Migrate pages for pid %d\n", pid);
            ret = numa_migrate_pages(pid, nodes_from, nodes_to);
            if (ret < 0) {
                printf("Migrate pages for pid %d failed\n", pid);
                break;
            }
        }
    }
    fclose(fp);

    // THAWED the CPU
    fp = fopen(cgpath_freezer, "w");
    if (fp == NULL) {
        printf("Failed to open file %s\n", cgpath_freezer);
        return 1;
    }
    printf("Write THAWED\n");
    ret = fputs("THAWED", fp);
    if (ret < 0)
        printf("Failed to write THAWED\n");
    fclose(fp);

    return 0;
}
